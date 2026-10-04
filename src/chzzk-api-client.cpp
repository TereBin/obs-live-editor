#include "chzzk-api-client.hpp"

#include <plugin-support.h>

#include <QDateTime>
#include <QDesktopServices>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrlQuery>

#include <utility>

namespace {
constexpr auto kApiBase = "https://openapi.chzzk.naver.com";
constexpr auto kUserProfilePath = "/open/v1/users/me";
constexpr auto kLiveSettingsPath = "/open/v1/lives/setting";
constexpr qint64 kDefaultTokenLifetimeSeconds = 86400;
constexpr qint64 kTokenRefreshMarginSeconds = 60;

QJsonObject unwrap(const QJsonObject &object)
{
	const QJsonValue content = object.value(QStringLiteral("content"));
	return content.isObject() ? content.toObject() : object;
}

QString apiError(const QJsonObject &object, const QString &fallback)
{
	const QString message = object.value(QStringLiteral("message")).toString();
	return message.isEmpty() ? fallback : message;
}

BroadcastSettings broadcastSettingsFrom(const QJsonObject &object)
{
	BroadcastSettings settings;
	settings.title = object.value(QStringLiteral("defaultLiveTitle")).toString();
	const QJsonObject category = object.value(QStringLiteral("category")).toObject();
	settings.categoryType = category.value(QStringLiteral("categoryType")).toString();
	settings.categoryId = category.value(QStringLiteral("categoryId")).toString();
	settings.categoryName = category.value(QStringLiteral("categoryValue")).toString();
	for (const QJsonValue &tag : object.value(QStringLiteral("tags")).toArray())
		settings.tags.append(tag.toString());
	return settings;
}

QVector<ChzzkCategory> categoriesFrom(const QJsonObject &object)
{
	QVector<ChzzkCategory> categories;
	for (const QJsonValue &value : object.value(QStringLiteral("data")).toArray()) {
		const QJsonObject item = value.toObject();
		categories.append({item.value(QStringLiteral("categoryType")).toString(),
				   item.value(QStringLiteral("categoryId")).toString(),
				   item.value(QStringLiteral("categoryValue")).toString(),
				   item.value(QStringLiteral("posterImageUrl")).toString()});
	}
	return categories;
}
} // namespace

ChzzkApiClient::ChzzkApiClient(QObject *parent) : QObject(parent)
{
	QString loadError;
	if (!tokenStore_.load(tokens_, loadError)) {
		tokenStore_.clear();
		if (!loadError.isEmpty())
			QMetaObject::invokeMethod(
				this, [this, loadError]() { fail(loadError); }, Qt::QueuedConnection);
	}

	connect(&callbackServer_, &OAuthCallbackServer::callbackReceived, this,
		[this](const QString &code, const QString &state, const QString &oauthError) {
			if (!oauthError.isEmpty()) {
				fail(QStringLiteral("치지직 로그인이 취소되었거나 실패했습니다: %1").arg(oauthError));
				return;
			}
			if (state.isEmpty() || state != pendingState_) {
				fail(QStringLiteral("로그인 검증값이 일치하지 않습니다. 다시 시도해 주세요."));
				return;
			}
			exchangeAuthorizationCode(code, state);
		});

	QMetaObject::invokeMethod(
		this, [this]() { emit loginStateChanged(isLoggedIn()); }, Qt::QueuedConnection);
}

QUrl ChzzkApiClient::brokerUrl(const QString &path) const
{
	QString base = QString::fromUtf8(CHZZK_BROKER_URL);
	while (base.endsWith('/'))
		base.chop(1);
	return QUrl(base + path);
}

QUrl ChzzkApiClient::apiUrl(const QString &path) const
{
	return QUrl(QString::fromUtf8(kApiBase) + path);
}

QNetworkRequest ChzzkApiClient::jsonRequest(const QUrl &url) const
{
	QNetworkRequest request(url);
	request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
	request.setRawHeader("Accept", "application/json");
	request.setTransferTimeout(15000);
	return request;
}

QNetworkRequest ChzzkApiClient::brokerRequest(const QString &path, const QString &token) const
{
	QNetworkRequest request = jsonRequest(brokerUrl(path));
	request.setRawHeader("X-OBS-Live-Editor-Version", QByteArray(PLUGIN_VERSION));
	if (!token.isEmpty())
		request.setRawHeader("Authorization", "Bearer " + token.toUtf8());
	return request;
}

QNetworkRequest ChzzkApiClient::authorizedRequest(const QUrl &url, const QString &token) const
{
	QNetworkRequest request = jsonRequest(url);
	request.setRawHeader("Authorization", "Bearer " + token.toUtf8());
	return request;
}

QJsonObject ChzzkApiClient::responseObject(QNetworkReply *reply, QString &error)
{
	const QByteArray body = reply->readAll();
	QJsonParseError parseError;
	const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
	const QJsonObject object = document.isObject() ? document.object() : QJsonObject{};
	if (object.value(QStringLiteral("code")).toString() == QStringLiteral("CLIENT_UPDATE_REQUIRED")) {
		emit clientUpdateRequired(object.value(QStringLiteral("latestVersion")).toString(),
					  object.value(QStringLiteral("minimumVersion")).toString(),
					  object.value(QStringLiteral("level")).toString(),
					  object.value(QStringLiteral("message")).toString(),
					  QUrl(object.value(QStringLiteral("releaseUrl")).toString()));
	}
	if (reply->error() != QNetworkReply::NoError) {
		error = apiError(object, reply->errorString());
		return object;
	}
	if (parseError.error != QJsonParseError::NoError && !body.trimmed().isEmpty())
		error = QStringLiteral("서버 응답을 해석하지 못했습니다.");
	return object;
}

void ChzzkApiClient::beginLogin()
{
	if (QString::fromUtf8(CHZZK_BROKER_URL).contains(QStringLiteral("YOUR-WORKER"))) {
		fail(QStringLiteral("빌드에 토큰 중계 Worker 주소가 설정되지 않았습니다."));
		return;
	}

	QString error;
	if (!callbackServer_.start(error)) {
		fail(error);
		return;
	}

	beginOperation();
	QNetworkReply *reply = network_.get(brokerRequest(QStringLiteral("/oauth/start")));
	connect(reply, &QNetworkReply::finished, this, [this, reply]() {
		QString error;
		const QJsonObject object = responseObject(reply, error);
		reply->deleteLater();
		endOperation();
		if (!error.isEmpty()) {
			callbackServer_.stop();
			fail(QStringLiteral("로그인을 시작하지 못했습니다: %1").arg(error));
			return;
		}

		pendingState_ = object.value(QStringLiteral("state")).toString();
		const QUrl authorizationUrl(object.value(QStringLiteral("authorizationUrl")).toString());
		if (pendingState_.isEmpty() || !authorizationUrl.isValid() ||
		    !QDesktopServices::openUrl(authorizationUrl)) {
			callbackServer_.stop();
			pendingState_.clear();
			fail(QStringLiteral("치지직 로그인 페이지를 열지 못했습니다."));
		}
	});
}

void ChzzkApiClient::exchangeAuthorizationCode(const QString &code, const QString &state)
{
	beginOperation();
	const QJsonObject body{{QStringLiteral("grantType"), QStringLiteral("authorization_code")},
			       {QStringLiteral("code"), code},
			       {QStringLiteral("state"), state}};
	QNetworkReply *reply = network_.post(brokerRequest(QStringLiteral("/oauth/token")),
					     QJsonDocument(body).toJson(QJsonDocument::Compact));
	connect(reply, &QNetworkReply::finished, this, [this, reply]() {
		QString error;
		const QJsonObject object = responseObject(reply, error);
		reply->deleteLater();
		endOperation();
		pendingState_.clear();
		if (!error.isEmpty()) {
			fail(QStringLiteral("토큰을 발급받지 못했습니다: %1").arg(error));
			return;
		}
		if (!saveTokens(object))
			return;
		emit loginStateChanged(true);
		emit operationSucceeded(QStringLiteral("치지직에 로그인했습니다."));
		loadUserProfile();
		loadSettings();
	});
}

bool ChzzkApiClient::saveTokens(const QJsonObject &rawObject)
{
	const QJsonObject object = unwrap(rawObject);
	ChzzkTokens updated = tokens_;
	updated.accessToken = object.value(QStringLiteral("accessToken")).toString();
	const QString refreshToken = object.value(QStringLiteral("refreshToken")).toString();
	const QString brokerToken = object.value(QStringLiteral("brokerToken")).toString();
	if (!refreshToken.isEmpty())
		updated.refreshToken = refreshToken;
	if (!brokerToken.isEmpty())
		updated.brokerToken = brokerToken;
	const QJsonValue expiresValue = object.value(QStringLiteral("expiresIn"));
	const qint64 expiresIn = expiresValue.isString()
					 ? expiresValue.toString().toLongLong()
					 : static_cast<qint64>(expiresValue.toDouble(kDefaultTokenLifetimeSeconds));
	updated.expiresAt = QDateTime::currentSecsSinceEpoch() + expiresIn;
	if (updated.isEmpty()) {
		fail(QStringLiteral("토큰 응답에 필요한 값이 없습니다."));
		return false;
	}

	QString error;
	if (!tokenStore_.save(updated, error)) {
		fail(error);
		return false;
	}
	tokens_ = std::move(updated);
	return true;
}

void ChzzkApiClient::ensureAccessToken(TokenCallback callback)
{
	if (tokens_.isEmpty()) {
		fail(QStringLiteral("먼저 치지직에 로그인해 주세요."));
		return;
	}
	if (tokens_.expiresAt > QDateTime::currentSecsSinceEpoch() + kTokenRefreshMarginSeconds) {
		callback(tokens_.accessToken);
		return;
	}

	pendingTokenCallbacks_.append(std::move(callback));
	if (!refreshing_)
		refreshAccessToken();
}

void ChzzkApiClient::refreshAccessToken()
{
	refreshing_ = true;
	beginOperation();
	const QJsonObject body{{QStringLiteral("grantType"), QStringLiteral("refresh_token")},
			       {QStringLiteral("refreshToken"), tokens_.refreshToken}};
	QNetworkReply *reply = network_.post(brokerRequest(QStringLiteral("/oauth/token")),
					     QJsonDocument(body).toJson(QJsonDocument::Compact));
	connect(reply, &QNetworkReply::finished, this, [this, reply]() { finishTokenRequest(reply); });
}

void ChzzkApiClient::finishTokenRequest(QNetworkReply *reply)
{
	QString error;
	const QJsonObject object = responseObject(reply, error);
	reply->deleteLater();
	refreshing_ = false;
	endOperation();
	if (!error.isEmpty()) {
		pendingTokenCallbacks_.clear();
		fail(QStringLiteral("로그인을 갱신하지 못했습니다. 다시 로그인해 주세요: %1").arg(error));
		return;
	}

	if (!saveTokens(object)) {
		pendingTokenCallbacks_.clear();
		return;
	}
	const auto callbacks = pendingTokenCallbacks_;
	pendingTokenCallbacks_.clear();
	for (const auto &callback : callbacks)
		callback(tokens_.accessToken);
}

void ChzzkApiClient::loadSettings()
{
	loadSettingsInternal(false);
}

void ChzzkApiClient::loadUserProfile()
{
	ensureAccessToken([this](const QString &token) {
		QNetworkRequest request = authorizedRequest(apiUrl(QString::fromUtf8(kUserProfilePath)), token);
		QNetworkReply *reply = network_.get(request);
		connect(reply, &QNetworkReply::finished, this, [this, reply]() {
			QString error;
			const QJsonObject object = unwrap(responseObject(reply, error));
			reply->deleteLater();
			if (!error.isEmpty())
				return;
			const QString channelName = object.value(QStringLiteral("channelName")).toString().trimmed();
			if (!channelName.isEmpty())
				emit userProfileLoaded(channelName);
		});
	});
}

void ChzzkApiClient::loadSettingsInternal(bool afterApply)
{
	ensureAccessToken([this, afterApply](const QString &token) {
		beginOperation();
		QNetworkRequest request = authorizedRequest(apiUrl(QString::fromUtf8(kLiveSettingsPath)), token);
		QNetworkReply *reply = network_.get(request);
		connect(reply, &QNetworkReply::finished, this, [this, reply, afterApply]() {
			QString error;
			const QJsonObject object = unwrap(responseObject(reply, error));
			reply->deleteLater();
			endOperation();
			if (!error.isEmpty()) {
				fail(QStringLiteral("방송 정보를 불러오지 못했습니다: %1").arg(error));
				return;
			}

			emit settingsLoaded(broadcastSettingsFrom(object), afterApply);
		});
	});
}

void ChzzkApiClient::searchCategories(const QString &query)
{
	const QString requestedQuery = query.trimmed();
	if (categoryReply_)
		categoryReply_->abort();
	if (requestedQuery.size() < 2 || tokens_.brokerToken.isEmpty()) {
		emit categoriesLoaded(requestedQuery, {});
		return;
	}
	QUrl url = brokerUrl(QStringLiteral("/categories"));
	QUrlQuery urlQuery;
	urlQuery.addQueryItem(QStringLiteral("query"), requestedQuery);
	url.setQuery(urlQuery);
	QNetworkRequest request = brokerRequest(QStringLiteral("/categories"), tokens_.brokerToken);
	request.setUrl(url);
	QNetworkReply *reply = network_.get(request);
	categoryReply_ = reply;
	connect(reply, &QNetworkReply::finished, this, [this, reply, requestedQuery]() {
		const QNetworkReply::NetworkError networkError = reply->error();
		QString error;
		const QJsonObject object = unwrap(responseObject(reply, error));
		if (categoryReply_ == reply)
			categoryReply_.clear();
		reply->deleteLater();
		if (networkError == QNetworkReply::OperationCanceledError)
			return;
		if (!error.isEmpty()) {
			fail(QStringLiteral("카테고리를 검색하지 못했습니다: %1").arg(error));
			return;
		}
		emit categoriesLoaded(requestedQuery, categoriesFrom(object));
	});
}

void ChzzkApiClient::updateSettings(const BroadcastSettings &settings, bool removeCategory)
{
	ensureAccessToken([this, settings, removeCategory](const QString &token) {
		QJsonObject body{{QStringLiteral("defaultLiveTitle"), settings.title}};
		QJsonArray tags;
		for (const QString &tag : settings.tags)
			tags.append(tag);
		body.insert(QStringLiteral("tags"), tags);
		if (removeCategory) {
			body.insert(QStringLiteral("categoryId"), QString());
		} else if (!settings.categoryId.isEmpty()) {
			body.insert(QStringLiteral("categoryType"), settings.categoryType);
			body.insert(QStringLiteral("categoryId"), settings.categoryId);
		}

		beginOperation();
		QNetworkRequest request = authorizedRequest(apiUrl(QString::fromUtf8(kLiveSettingsPath)), token);
		QNetworkReply *reply = network_.sendCustomRequest(request, "PATCH",
								  QJsonDocument(body).toJson(QJsonDocument::Compact));
		connect(reply, &QNetworkReply::finished, this, [this, reply]() {
			QString error;
			responseObject(reply, error);
			reply->deleteLater();
			endOperation();
			if (!error.isEmpty()) {
				fail(QStringLiteral("방송 정보를 적용하지 못했습니다: %1").arg(error));
				return;
			}
			emit operationSucceeded(QStringLiteral("방송 정보를 적용했습니다."));
			loadSettingsInternal(true);
		});
	});
}

void ChzzkApiClient::logout()
{
	if (categoryReply_)
		categoryReply_->abort();
	if (!tokens_.accessToken.isEmpty()) {
		const QJsonObject body{{QStringLiteral("token"), tokens_.accessToken},
				       {QStringLiteral("tokenTypeHint"), QStringLiteral("access_token")}};
		QNetworkReply *reply = network_.post(brokerRequest(QStringLiteral("/oauth/revoke")),
						     QJsonDocument(body).toJson(QJsonDocument::Compact));
		connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
	}
	tokens_ = {};
	tokenStore_.clear();
	emit loginStateChanged(false);
	emit operationSucceeded(QStringLiteral("로그아웃했습니다."));
}

void ChzzkApiClient::fail(const QString &message)
{
	emit operationFailed(message);
}

void ChzzkApiClient::beginOperation()
{
	if (activeOperations_++ == 0)
		emit busyChanged(true);
}

void ChzzkApiClient::endOperation()
{
	Q_ASSERT(activeOperations_ > 0);
	if (activeOperations_ > 0 && --activeOperations_ == 0)
		emit busyChanged(false);
}
