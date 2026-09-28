#pragma once

#include <QObject>
#include <QTcpServer>

class OAuthCallbackServer final : public QObject {
	Q_OBJECT

public:
	explicit OAuthCallbackServer(QObject *parent = nullptr);
	bool start(QString &error);
	void stop();

signals:
	void callbackReceived(const QString &code, const QString &state, const QString &error);

private slots:
	void acceptConnection();

private:
	QTcpServer server_;
};
