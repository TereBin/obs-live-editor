#include "token-store.hpp"

#include <obs-module.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#ifdef _WIN32
#include <windows.h>
#include <dpapi.h>
#endif

namespace {
QByteArray protect(const QByteArray &plain, QString &error)
{
#ifdef _WIN32
	DATA_BLOB input{static_cast<DWORD>(plain.size()), reinterpret_cast<BYTE *>(const_cast<char *>(plain.data()))};
	DATA_BLOB output{};
	if (!CryptProtectData(&input, L"OBS Live Editor tokens", nullptr, nullptr, nullptr,
			       CRYPTPROTECT_UI_FORBIDDEN, &output)) {
		error = QStringLiteral("Windows에서 로그인 정보를 암호화하지 못했습니다. (%1)").arg(GetLastError());
		return {};
	}
	QByteArray encrypted(reinterpret_cast<const char *>(output.pbData), static_cast<int>(output.cbData));
	LocalFree(output.pbData);
	return encrypted;
#else
	Q_UNUSED(plain);
	error = QStringLiteral("이 빌드에서는 안전한 토큰 저장소를 지원하지 않습니다.");
	return {};
#endif
}

QByteArray unprotect(const QByteArray &encrypted, QString &error)
{
#ifdef _WIN32
	DATA_BLOB input{static_cast<DWORD>(encrypted.size()),
			reinterpret_cast<BYTE *>(const_cast<char *>(encrypted.data()))};
	DATA_BLOB output{};
	if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output)) {
		error = QStringLiteral("저장된 로그인 정보를 복호화하지 못했습니다. 다시 로그인해 주세요.");
		return {};
	}
	QByteArray plain(reinterpret_cast<const char *>(output.pbData), static_cast<int>(output.cbData));
	LocalFree(output.pbData);
	return plain;
#else
	Q_UNUSED(encrypted);
	error = QStringLiteral("이 빌드에서는 안전한 토큰 저장소를 지원하지 않습니다.");
	return {};
#endif
}
} // namespace

QString TokenStore::storagePath() const
{
	char *path = obs_module_config_path("credentials.bin");
	const QString result = QString::fromUtf8(path);
	bfree(path);
	return result;
}

bool TokenStore::load(ChzzkTokens &tokens, QString &error) const
{
	QFile file(storagePath());
	if (!file.exists())
		return true;
	if (!file.open(QIODevice::ReadOnly)) {
		error = QStringLiteral("저장된 로그인 정보를 읽지 못했습니다.");
		return false;
	}

	const QByteArray plain = unprotect(file.readAll(), error);
	if (plain.isEmpty())
		return false;

	QJsonParseError parseError;
	const QJsonDocument document = QJsonDocument::fromJson(plain, &parseError);
	if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
		error = QStringLiteral("저장된 로그인 정보가 손상되었습니다. 다시 로그인해 주세요.");
		return false;
	}

	const QJsonObject object = document.object();
	tokens.accessToken = object.value(QStringLiteral("accessToken")).toString();
	tokens.refreshToken = object.value(QStringLiteral("refreshToken")).toString();
	tokens.brokerToken = object.value(QStringLiteral("brokerToken")).toString();
	tokens.expiresAt = static_cast<qint64>(object.value(QStringLiteral("expiresAt")).toDouble());
	return true;
}

bool TokenStore::save(const ChzzkTokens &tokens, QString &error) const
{
	const QJsonObject object{{QStringLiteral("accessToken"), tokens.accessToken},
				 {QStringLiteral("refreshToken"), tokens.refreshToken},
				 {QStringLiteral("brokerToken"), tokens.brokerToken},
				 {QStringLiteral("expiresAt"), static_cast<double>(tokens.expiresAt)}};
	const QByteArray encrypted = protect(QJsonDocument(object).toJson(QJsonDocument::Compact), error);
	if (encrypted.isEmpty())
		return false;

	const QString path = storagePath();
	QDir().mkpath(QFileInfo(path).absolutePath());
	QSaveFile file(path);
	if (!file.open(QIODevice::WriteOnly) || file.write(encrypted) != encrypted.size() || !file.commit()) {
		error = QStringLiteral("로그인 정보를 디스크에 저장하지 못했습니다.");
		return false;
	}
	return true;
}

void TokenStore::clear() const
{
	QFile::remove(storagePath());
}
