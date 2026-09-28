#include "oauth-callback-server.hpp"

#include <QHostAddress>
#include <QTcpSocket>
#include <QUrl>
#include <QUrlQuery>

namespace {
constexpr quint16 kCallbackPort = 20132;
}

OAuthCallbackServer::OAuthCallbackServer(QObject *parent) : QObject(parent)
{
	connect(&server_, &QTcpServer::newConnection, this, &OAuthCallbackServer::acceptConnection);
}

bool OAuthCallbackServer::start(QString &error)
{
	if (server_.isListening())
		return true;
	if (!server_.listen(QHostAddress::LocalHost, kCallbackPort)) {
		error = QStringLiteral("로그인 콜백 포트 %1을 열 수 없습니다: %2")
				.arg(kCallbackPort)
				.arg(server_.errorString());
		return false;
	}
	return true;
}

void OAuthCallbackServer::stop()
{
	server_.close();
}

void OAuthCallbackServer::acceptConnection()
{
	QTcpSocket *socket = server_.nextPendingConnection();
	connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
		const QByteArray request = socket->readAll();
		const QList<QByteArray> lines = request.split('\n');
		const QList<QByteArray> requestLine = lines.value(0).trimmed().split(' ');

		QString code;
		QString state;
		QString oauthError;
		if (requestLine.size() >= 2) {
			const QUrl url(QStringLiteral("http://127.0.0.1") + QString::fromUtf8(requestLine.at(1)));
			const QUrlQuery query(url);
			code = query.queryItemValue(QStringLiteral("code"));
			state = query.queryItemValue(QStringLiteral("state"));
			oauthError = query.queryItemValue(QStringLiteral("error"));
		}

		const bool ok = !code.isEmpty() && oauthError.isEmpty();
		const QByteArray body = ok
					? QByteArray("<!doctype html><meta charset=utf-8><title>Login complete</title>"
						     "<p>로그인이 완료되었습니다. 이 창을 닫고 OBS로 돌아가세요.</p>")
					: QByteArray("<!doctype html><meta charset=utf-8><title>Login failed</title>"
						     "<p>로그인에 실패했습니다. OBS에서 다시 시도해 주세요.</p>");
		const QByteArray response = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n"
					    "Cache-Control: no-store\r\nConnection: close\r\nContent-Length: " +
					    QByteArray::number(body.size()) + "\r\n\r\n" + body;
		socket->write(response);
		socket->disconnectFromHost();
		server_.close();
		emit callbackReceived(code, state, oauthError);
	});
	connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
}
