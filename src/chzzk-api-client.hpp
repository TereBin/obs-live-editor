#pragma once

#include "chzzk-types.hpp"
#include "oauth-callback-server.hpp"
#include "token-store.hpp"

#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QVector>

#include <functional>

class QJsonObject;
class QNetworkReply;
class QNetworkRequest;
class QUrl;

class ChzzkApiClient final : public QObject {
	Q_OBJECT

public:
	explicit ChzzkApiClient(QObject *parent = nullptr);

	bool isLoggedIn() const { return !tokens_.isEmpty(); }
	void beginLogin();
	void logout();
	void loadSettings();
	void searchCategories(const QString &query);
	void updateSettings(const BroadcastSettings &settings, bool removeCategory);

signals:
	void loginStateChanged(bool loggedIn);
	void busyChanged(bool busy);
	void settingsLoaded(const BroadcastSettings &settings);
	void categoriesLoaded(const QString &query, const QVector<ChzzkCategory> &categories);
	void operationSucceeded(const QString &message);
	void operationFailed(const QString &message);

private:
	using TokenCallback = std::function<void(const QString &)>;

	QUrl brokerUrl(const QString &path) const;
	QUrl apiUrl(const QString &path) const;
	QNetworkRequest jsonRequest(const QUrl &url) const;
	QNetworkRequest authorizedRequest(const QUrl &url, const QString &token) const;
	QJsonObject responseObject(QNetworkReply *reply, QString &error) const;
	void exchangeAuthorizationCode(const QString &code, const QString &state);
	void ensureAccessToken(TokenCallback callback);
	void refreshAccessToken();
	void finishTokenRequest(QNetworkReply *reply);
	bool saveTokens(const QJsonObject &object);
	void fail(const QString &message);
	void beginOperation();
	void endOperation();

	QNetworkAccessManager network_;
	OAuthCallbackServer callbackServer_;
	TokenStore tokenStore_;
	ChzzkTokens tokens_;
	QString pendingState_;
	QVector<TokenCallback> pendingTokenCallbacks_;
	QPointer<QNetworkReply> categoryReply_;
	int activeOperations_ = 0;
	bool refreshing_ = false;
};
