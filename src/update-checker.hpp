#pragma once

#include "update-info.hpp"

#include <QNetworkAccessManager>
#include <QObject>

class UpdateChecker final : public QObject {
	Q_OBJECT

public:
	explicit UpdateChecker(QObject *parent = nullptr);

	void checkForUpdates(bool force = false);
	void skipVersion(const QString &version);

signals:
	void updateAvailable(const UpdateInfo &info);

private:
	QString settingsPath() const;
	bool checkedRecently() const;
	void handleManifest(const QByteArray &body);

	QNetworkAccessManager network_;
};
