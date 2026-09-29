#include "update-checker.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QVersionNumber>

namespace {
constexpr auto kManifestUrl =
	"https://raw.githubusercontent.com/TereBin/obs-live-editor-releases/main/update-manifest.json";
constexpr auto kDefaultReleaseUrl = "https://github.com/TereBin/obs-live-editor-releases/releases/latest";
constexpr qint64 kCheckIntervalSeconds = 24 * 60 * 60;

UpdateLevel updateLevelFrom(const QString &value)
{
	if (value == QStringLiteral("security"))
		return UpdateLevel::Security;
	if (value == QStringLiteral("required"))
		return UpdateLevel::Required;
	if (value == QStringLiteral("recommended"))
		return UpdateLevel::Recommended;
	return UpdateLevel::Optional;
}

bool isValidReleaseUrl(const QUrl &url)
{
	return url.scheme() == QStringLiteral("https") && url.host() == QStringLiteral("github.com");
}
} // namespace

UpdateChecker::UpdateChecker(QObject *parent) : QObject(parent) {}

QString UpdateChecker::settingsPath() const
{
	char *path = obs_module_config_path("settings.ini");
	const QString result = QString::fromUtf8(path);
	bfree(path);
	return result;
}

bool UpdateChecker::checkedRecently() const
{
	QSettings settings(settingsPath(), QSettings::IniFormat);
	const qint64 lastCheckedAt = settings.value(QStringLiteral("updates/lastCheckedAt"), 0).toLongLong();
	return lastCheckedAt > QDateTime::currentSecsSinceEpoch() - kCheckIntervalSeconds;
}

void UpdateChecker::checkForUpdates(bool force)
{
	if (!force && checkedRecently())
		return;

	QNetworkRequest request(QUrl(QString::fromUtf8(kManifestUrl)));
	request.setRawHeader("Accept", "application/json");
	request.setRawHeader("Cache-Control", "no-cache");
	request.setRawHeader("User-Agent", QByteArray("obs-live-editor/") + PLUGIN_VERSION);
	request.setTransferTimeout(10000);
	QNetworkReply *reply = network_.get(request);
	connect(reply, &QNetworkReply::finished, this, [this, reply]() {
		const QByteArray body = reply->readAll();
		const bool succeeded = reply->error() == QNetworkReply::NoError;
		reply->deleteLater();
		if (!succeeded)
			return;

		QSettings settings(settingsPath(), QSettings::IniFormat);
		QDir().mkpath(QFileInfo(settings.fileName()).absolutePath());
		settings.setValue(QStringLiteral("updates/lastCheckedAt"), QDateTime::currentSecsSinceEpoch());
		handleManifest(body);
	});
}

void UpdateChecker::skipVersion(const QString &version)
{
	QSettings settings(settingsPath(), QSettings::IniFormat);
	QDir().mkpath(QFileInfo(settings.fileName()).absolutePath());
	settings.setValue(QStringLiteral("updates/skippedVersion"), version);
}

void UpdateChecker::handleManifest(const QByteArray &body)
{
	QJsonParseError parseError;
	const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
	if (parseError.error != QJsonParseError::NoError || !document.isObject())
		return;

	const QJsonObject object = document.object();
	UpdateInfo info;
	info.latestVersion = object.value(QStringLiteral("latestVersion")).toString();
	info.minimumVersion = object.value(QStringLiteral("minimumSupportedVersion")).toString();
	info.message = object.value(QStringLiteral("message")).toString();
	info.releaseUrl = QUrl(object.value(QStringLiteral("releaseUrl")).toString());
	info.level = updateLevelFrom(object.value(QStringLiteral("level")).toString().toLower());

	const QVersionNumber current = QVersionNumber::fromString(QString::fromUtf8(PLUGIN_VERSION));
	const QVersionNumber latest = QVersionNumber::fromString(info.latestVersion);
	const QVersionNumber minimum = QVersionNumber::fromString(info.minimumVersion);
	if (current.isNull() || latest.isNull() || QVersionNumber::compare(current, latest) >= 0)
		return;
	if (!minimum.isNull() && QVersionNumber::compare(current, minimum) < 0 && !info.blocksUse())
		info.level = UpdateLevel::Required;
	if (!isValidReleaseUrl(info.releaseUrl))
		info.releaseUrl = QUrl(QString::fromUtf8(kDefaultReleaseUrl));

	QSettings settings(settingsPath(), QSettings::IniFormat);
	const QString skippedVersion = settings.value(QStringLiteral("updates/skippedVersion")).toString();
	if (!info.blocksUse() && skippedVersion == info.latestVersion)
		return;
	emit updateAvailable(info);
}
