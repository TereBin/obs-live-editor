#pragma once

#include "chzzk-types.hpp"

#include <QString>

class TokenStore {
public:
	bool load(ChzzkTokens &tokens, QString &error) const;
	bool save(const ChzzkTokens &tokens, QString &error) const;
	void clear() const;

private:
	QString storagePath() const;
};
