#include "chzzk-dock.hpp"

#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputMethodEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

namespace {
constexpr int kCategoryTypeRole = Qt::UserRole + 1;
constexpr int kCategoryIdRole = Qt::UserRole + 2;
constexpr int kCategorySearchDelayMs = 350;
}

void CategoryLineEdit::inputMethodEvent(QInputMethodEvent *event)
{
	const bool composing = !event->preeditString().isEmpty();
	const bool changed = composing_ != composing;
	composing_ = composing;
	QLineEdit::inputMethodEvent(event);
	if (changed)
		emit compositionChanged(composing_);
}

ChzzkDock::ChzzkDock(QWidget *parent) : QWidget(parent), client_(this)
{
	buildUi();
	connectUi();
	updateLoginState(client_.isLoggedIn());
	if (client_.isLoggedIn())
		QTimer::singleShot(0, &client_, &ChzzkApiClient::loadSettings);
}

void ChzzkDock::connectUi()
{
	connect(loginButton_, &QPushButton::clicked, &client_, &ChzzkApiClient::beginLogin);
	connect(logoutButton_, &QPushButton::clicked, &client_, &ChzzkApiClient::logout);
	connect(refreshButton_, &QPushButton::clicked, &client_, &ChzzkApiClient::loadSettings);
	connect(applyButton_, &QPushButton::clicked, this, &ChzzkDock::applyChanges);
	connect(clearCategoryButton_, &QPushButton::clicked, this, &ChzzkDock::clearCategory);
	connect(&client_, &ChzzkApiClient::loginStateChanged, this, &ChzzkDock::updateLoginState);
	connect(&client_, &ChzzkApiClient::settingsLoaded, this, &ChzzkDock::showSettings);
	connect(&client_, &ChzzkApiClient::categoriesLoaded, this, &ChzzkDock::showCategories);
	connect(&client_, &ChzzkApiClient::operationSucceeded, this, &ChzzkDock::showSuccess);
	connect(&client_, &ChzzkApiClient::operationFailed, this, &ChzzkDock::showError);
	connect(&client_, &ChzzkApiClient::busyChanged, this, [this](bool busy) {
		busy_ = busy;
		updateControls();
	});
	connect(categoryTimer_, &QTimer::timeout, this, [this]() {
		client_.searchCategories(categoryEdit_->text());
	});
	connect(categoryEdit_, &QLineEdit::textEdited, this, [this](const QString &) {
		resetCategorySelection(false);
		categoryCombo_->hidePopup();
		scheduleCategorySearch();
	});
	connect(categoryEdit_, &CategoryLineEdit::compositionChanged, this, [this](bool composing) {
		if (composing)
			categoryTimer_->stop();
		else
			scheduleCategorySearch();
	});
	connect(categoryCombo_, qOverload<int>(&QComboBox::activated), this, [this](int index) {
		selectedCategoryType_ = categoryCombo_->itemData(index, kCategoryTypeRole).toString();
		selectedCategoryId_ = categoryCombo_->itemData(index, kCategoryIdRole).toString();
		removeCategory_ = false;
	});
}

void ChzzkDock::buildUi()
{
	setMinimumWidth(300);
	auto *root = new QVBoxLayout(this);
	root->setContentsMargins(12, 12, 12, 12);
	root->setSpacing(10);

	auto *accountRow = new QHBoxLayout();
	statusLabel_ = new QLabel(this);
	statusLabel_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
	loginButton_ = new QPushButton(QStringLiteral("로그인"), this);
	logoutButton_ = new QPushButton(QStringLiteral("로그아웃"), this);
	accountRow->addWidget(statusLabel_);
	accountRow->addWidget(loginButton_);
	accountRow->addWidget(logoutButton_);
	root->addLayout(accountRow);

	auto *form = new QFormLayout();
	form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	titleEdit_ = new QLineEdit(this);
	titleEdit_->setPlaceholderText(QStringLiteral("방송 제목"));
	form->addRow(QStringLiteral("제목"), titleEdit_);

	auto *categoryRow = new QHBoxLayout();
	categoryCombo_ = new QComboBox(this);
	categoryCombo_->setEditable(true);
	categoryEdit_ = new CategoryLineEdit(categoryCombo_);
	categoryCombo_->setLineEdit(categoryEdit_);
	categoryCombo_->setInsertPolicy(QComboBox::NoInsert);
	categoryCombo_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	categoryCombo_->setMinimumContentsLength(14);
	categoryEdit_->setPlaceholderText(QStringLiteral("두 글자 이상 검색"));
	clearCategoryButton_ = new QPushButton(this);
	clearCategoryButton_->setIcon(style()->standardIcon(QStyle::SP_DialogResetButton));
	clearCategoryButton_->setToolTip(QStringLiteral("카테고리 제거"));
	clearCategoryButton_->setFixedWidth(34);
	categoryRow->addWidget(categoryCombo_, 1);
	categoryRow->addWidget(clearCategoryButton_);
	form->addRow(QStringLiteral("카테고리"), categoryRow);

	tagsEdit_ = new QLineEdit(this);
	tagsEdit_->setPlaceholderText(QStringLiteral("쉼표로 구분"));
	form->addRow(QStringLiteral("태그"), tagsEdit_);
	root->addLayout(form);

	messageLabel_ = new QLabel(this);
	messageLabel_->setWordWrap(true);
	messageLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
	root->addWidget(messageLabel_);

	root->addStretch(1);
	auto *actionRow = new QHBoxLayout();
	refreshButton_ = new QPushButton(QStringLiteral("새로고침"), this);
	refreshButton_->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
	applyButton_ = new QPushButton(QStringLiteral("적용"), this);
	applyButton_->setDefault(true);
	actionRow->addWidget(refreshButton_);
	actionRow->addWidget(applyButton_);
	root->addLayout(actionRow);

	categoryTimer_ = new QTimer(this);
	categoryTimer_->setSingleShot(true);
	categoryTimer_->setInterval(kCategorySearchDelayMs);
}

void ChzzkDock::updateLoginState(bool loggedIn)
{
	loggedIn_ = loggedIn;
	statusLabel_->setText(loggedIn ? QStringLiteral("치지직 연결됨") : QStringLiteral("로그인 필요"));
	loginButton_->setVisible(!loggedIn);
	logoutButton_->setVisible(loggedIn);
	updateControls();
	if (!loggedIn) {
		titleEdit_->clear();
		categoryCombo_->clear();
		tagsEdit_->clear();
		resetCategorySelection(false);
	}
}

void ChzzkDock::updateControls()
{
	const bool canEdit = loggedIn_ && !busy_;
	loginButton_->setEnabled(!loggedIn_ && !busy_);
	logoutButton_->setEnabled(loggedIn_ && !busy_);
	titleEdit_->setEnabled(canEdit);
	categoryCombo_->setEnabled(canEdit);
	clearCategoryButton_->setEnabled(canEdit);
	tagsEdit_->setEnabled(canEdit);
	refreshButton_->setEnabled(canEdit);
	applyButton_->setEnabled(canEdit);
}

void ChzzkDock::scheduleCategorySearch()
{
	if (categoryEdit_->isComposing() || !loggedIn_) {
		categoryTimer_->stop();
		return;
	}
	categoryTimer_->start();
}

void ChzzkDock::resetCategorySelection(bool removeCategory)
{
	selectedCategoryId_.clear();
	selectedCategoryType_.clear();
	removeCategory_ = removeCategory;
}

void ChzzkDock::showSettings(const BroadcastSettings &settings)
{
	titleEdit_->setText(settings.title);
	selectedCategoryId_ = settings.categoryId;
	selectedCategoryType_ = settings.categoryType;
	removeCategory_ = false;
	{
		const QSignalBlocker blocker(categoryCombo_);
		categoryCombo_->clear();
		if (!settings.categoryId.isEmpty()) {
			categoryCombo_->addItem(settings.categoryName);
			categoryCombo_->setItemData(0, settings.categoryType, kCategoryTypeRole);
			categoryCombo_->setItemData(0, settings.categoryId, kCategoryIdRole);
			categoryCombo_->setCurrentIndex(0);
		}
	}
	tagsEdit_->setText(settings.tags.join(QStringLiteral(", ")));
	showSuccess(QStringLiteral("방송 정보를 불러왔습니다."));
}

void ChzzkDock::showCategories(const QString &query, const QVector<ChzzkCategory> &categories)
{
	const QString typed = categoryCombo_->currentText();
	if (categoryEdit_->isComposing() || typed.trimmed() != query)
		return;
	const QSignalBlocker blocker(categoryCombo_);
	categoryCombo_->clear();
	for (const ChzzkCategory &category : categories) {
		categoryCombo_->addItem(category.name);
		const int index = categoryCombo_->count() - 1;
		categoryCombo_->setItemData(index, category.type, kCategoryTypeRole);
		categoryCombo_->setItemData(index, category.id, kCategoryIdRole);
	}
	categoryCombo_->setEditText(typed);
	if (!categories.isEmpty())
		categoryCombo_->showPopup();
}

QStringList ChzzkDock::validatedTags(bool &valid) const
{
	valid = true;
	QStringList tags;
	const QRegularExpression allowed(QStringLiteral("^[\\p{L}\\p{N}]+$"),
					 QRegularExpression::UseUnicodePropertiesOption);
	for (QString tag : tagsEdit_->text().split(',', Qt::SkipEmptyParts)) {
		tag = tag.trimmed();
		if (!allowed.match(tag).hasMatch()) {
			valid = false;
			return {};
		}
		if (!tags.contains(tag))
			tags.append(tag);
	}
	return tags;
}

void ChzzkDock::applyChanges()
{
	const QString title = titleEdit_->text().trimmed();
	if (title.isEmpty()) {
		showError(QStringLiteral("방송 제목은 비워 둘 수 없습니다."));
		return;
	}
	if (!removeCategory_ && !categoryCombo_->currentText().trimmed().isEmpty() && selectedCategoryId_.isEmpty()) {
		showError(QStringLiteral("검색 결과에서 카테고리를 선택해 주세요."));
		return;
	}

	bool validTags = false;
	const QStringList tags = validatedTags(validTags);
	if (!validTags) {
		showError(QStringLiteral("태그에는 글자와 숫자만 사용할 수 있습니다."));
		return;
	}

	BroadcastSettings settings;
	settings.title = title;
	settings.categoryType = selectedCategoryType_;
	settings.categoryId = selectedCategoryId_;
	settings.tags = tags;
	client_.updateSettings(settings, removeCategory_);
}

void ChzzkDock::clearCategory()
{
	categoryTimer_->stop();
	categoryCombo_->hidePopup();
	categoryCombo_->clear();
	resetCategorySelection(true);
	showSuccess(QStringLiteral("적용하면 카테고리가 제거됩니다."));
}

void ChzzkDock::showSuccess(const QString &message)
{
	messageLabel_->setStyleSheet(QStringLiteral("color: palette(highlight);"));
	messageLabel_->setText(message);
}

void ChzzkDock::showError(const QString &message)
{
	messageLabel_->setStyleSheet(QStringLiteral("color: #d94b4b;"));
	messageLabel_->setText(message);
}
