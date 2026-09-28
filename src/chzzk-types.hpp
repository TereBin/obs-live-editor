#pragma once

#include <QString>
#include <QStringList>

struct ChzzkTokens {
	QString accessToken;
	QString refreshToken;
	QString brokerToken;
	qint64 expiresAt = 0;

	bool isEmpty() const { return accessToken.isEmpty() || refreshToken.isEmpty(); }
};

struct BroadcastSettings {
	QString title;
	QString categoryType;
	QString categoryId;
	QString categoryName;
	QStringList tags;
};

struct ChzzkCategory {
	QString type;
	QString id;
	QString name;
	QString posterUrl;
};
