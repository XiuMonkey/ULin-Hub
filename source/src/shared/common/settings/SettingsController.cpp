// 见 SettingsController.h：设置页数据面。
// 数据一律以 QVariantMap / QVariantList 摊平后暴露给 QML；改动集中 emit 对应 xxxChanged()，QML 不轮询。
// 写操作契约：setBusy(true) → RPC → 回调里 setBusy(false) 再 refresh()，刷新期间 QML 禁用表单。
// 四套反馈文案各管一处：m_notice 列表级瞬时提示（4s 自隐）、m_status 模型页状态行、
// m_formFeedback 表单内联提示、m_presetFeedback 预设下拉的失败文案。
// 账号面板状态机（服务端 account/getState 的 status 字段）：
// signed-out --startSignIn--> attempt(phase / authorizeUrl / id) --浏览器完成登录--> credential-stored；
// 只有 credential-stored 才继续拉 getProfile 与 getBalance，取消、登出或失败都先回到 signed-out。

#include "common/settings/SettingsController.h"

#include "common/appearance/ThemeManager.h"
#include "common/appearance/TranslationManager.h"
#include "common/agent/AgentPresetService.h"
#include "common/agent/ulincore/settings/UlinCoreSettings.h"
#include "common/settings/SettingsStore.h"
#include "common/agent/dsh/DshApiClient.h"

#include <QJsonArray>
#include <QRegularExpression>
#include <QCoreApplication>
#include <QDateTime>
#include <QQmlEngine>
#include <memory>
#include <QDesktopServices>
#include <QLocale>
#include <QUrl>
#include <QJsonObject>
#include <QTimer>
#include <QVariantMap>

SettingsController::SettingsController(DshApiClient* api, QObject* parent)
	: QObject(parent)
	, m_api(api)
{
	m_noticeTimer = new QTimer(this);  // 列表级瞬时提示：与原版 modelListNotice 一样 4 秒后自隐
	m_noticeTimer->setSingleShot(true);
	m_noticeTimer->setInterval(4000);
	connect(m_noticeTimer, &QTimer::timeout, this, [this]() {
		if (m_notice.isEmpty())
			return;
		m_notice.clear();
		emit noticeChanged();
		});

	// 默认态先铺满所有键：m_account 直接暴露给 QML，缺键的绑定会读到 undefined。
	m_account.insert(QStringLiteral("status"), QStringLiteral("signed-out"));
	m_account.insert(QStringLiteral("signedIn"), false);
	m_account.insert(QStringLiteral("busy"), false);
	m_account.insert(QStringLiteral("balances"), QVariantList());
	m_account.insert(QStringLiteral("error"), QString());
	for (const char* key : {"name", "contact", "avatarUrl", "balanceText", "bonusText",
		"attemptPhase", "attemptId", "usageUrl", "topUpUrl", "authorizeUrl"})
		m_account.insert(QString::fromLatin1(key), QString());
	// 语言列表必须在任何 QML 绑定之前就位：openPage() 先 emit openChanged()（QML Loader 同步装载
	// 设置页、语言下拉当场绑定），后 refresh() → loadAppearance() 才填数据；配合 CONSTANT 属性
	// 就会"永远空白且不报错"。
	loadAppearance();
	loadMasterBackend();
}

namespace
{
QJsonObject accountClientMetadata()
{
	QJsonObject client;
	// 随所有 account/* 请求上报的客户端标识（version / locale / timezoneOffsetSeconds），服务端据此做兼容与展示。
	const QString version = QCoreApplication::applicationVersion().isEmpty()
		? QStringLiteral("0.2.0") : QCoreApplication::applicationVersion();
	client.insert(QStringLiteral("version"), version);
	QString locale = QLocale::system().name();
	locale.replace(QLatin1Char('_'), QLatin1Char('-'));   // QLocale 给的是 zh_CN，协议要 BCP-47 的 zh-CN
	client.insert(QStringLiteral("locale"), locale);
	client.insert(QStringLiteral("timezoneOffsetSeconds"),
		QDateTime::currentDateTime().offsetFromUtc());
	return client;
}

QJsonObject accountClientArgs()
{
	// 所有 account/* 请求共用的 args 外壳：只有一个 client 对象，字段固定。
	QJsonObject args;
	args.insert(QStringLiteral("client"), accountClientMetadata());
	return args;
}

QString formatBalanceAmount(const QString& raw)
{
	// 服务端余额是高精度小数；界面上只保留两位，避免 12.345678901 这种长串。
	bool ok = false;
	const double value = raw.trimmed().toDouble(&ok);
	if (!ok)
		return raw;
	return QString::number(value, 'f', 2);
}

QString walletText(const QJsonArray& wallets)
{
	// 钱包列表转单行摘要：余额为空的是服务端占位项，跳过；CNY 用 ¥，其它币种保留原始代码。
	QStringList parts;
	for (const QJsonValue& value : wallets)
	{
		const QJsonObject wallet = value.toObject();
		const QString balance = wallet.value(QStringLiteral("balance")).toString();
		const QString currency = wallet.value(QStringLiteral("currency")).toString();
		if (balance.isEmpty())
			continue;
		const QString amount = formatBalanceAmount(balance);
		parts.append(currency == QLatin1String("CNY")
			? QStringLiteral("¥%1").arg(amount)
			: QStringLiteral("%1 %2").arg(amount, currency));
	}
	return parts.join(QStringLiteral(" · "));
}
} // namespace

void SettingsController::openAccountPage()
{
	// 已打开时只清提示不重拉：数据由 refreshAccount() 的 m_accountBusy 闸门去重。
	// 登录提示 = 浏览器已拉起但还没登录成功时，账号按钮上挂的那条提示。
	m_accountLoginHint = false;
	emit accountLoginHintChanged();
	if (m_accountOpen)
		return;
	m_accountOpen = true;
	emit accountOpenChanged();
	refreshAccount();
}

void SettingsController::dismissAccountLoginHint()
{
	if (!m_accountLoginHint)
		return;
	m_accountLoginHint = false;
	emit accountLoginHintChanged();
}

void SettingsController::closeAccountPage()
{
	// 只关面板，不取消进行中的登录 attempt：用户可能还要在浏览器里把登录走完。
	if (!m_accountOpen)
		return;
	m_accountOpen = false;
	emit accountOpenChanged();
}

void SettingsController::refreshAccount()
{
	// m_accountBusy 一次只放行一组 RPC：既挡重复刷新，也挡住刷新期间的登录/登出。
	if (!m_api || m_accountBusy)
		return;
	m_accountBusy = true;
	m_account.insert(QStringLiteral("busy"), true);
	m_account.insert(QStringLiteral("error"), QString());
	emit accountChanged();

	// getState 是账号面板的总入口：它定 signedIn、登录 attempt 与外链，确认已登录后才继续补资料与余额。
	m_api->callMethod(QStringLiteral("account/getState"), QJsonObject(),
		[this](const QJsonObject& value) {
			m_account.insert(QStringLiteral("status"),
				value.value(QStringLiteral("status")).toString());
			const bool signedIn = value.value(QStringLiteral("status")).toString()
				== QLatin1String("credential-stored");
			m_account.insert(QStringLiteral("signedIn"), signedIn);
			// 登录已成功：旧提示必须自己消失，不能等用户再点开一次。
			if (signedIn && m_accountLoginHint)
			{
				m_accountLoginHint = false;
				emit accountLoginHintChanged();
			}
			const QJsonObject links = value.value(QStringLiteral("links")).toObject();   // 用量页 / 充值页地址，缺失时是空串，界面据此隐藏按钮
			m_account.insert(QStringLiteral("usageUrl"),
				links.value(QStringLiteral("usageUrl")).toString());
			m_account.insert(QStringLiteral("topUpUrl"),
				links.value(QStringLiteral("topUpUrl")).toString());
			const QJsonObject attempt = value.value(QStringLiteral("attempt")).toObject();
			m_account.insert(QStringLiteral("attemptPhase"),
				attempt.value(QStringLiteral("phase")).toString());
			const QString authorizeUrl = attempt.value(QStringLiteral("authorizeUrl")).toString();
			m_account.insert(QStringLiteral("authorizeUrl"), authorizeUrl);
			// 与上次不同 = 服务端起了新的登录 attempt：请宿主开浏览器；同一个 URL 不重复弹。
			if (!authorizeUrl.isEmpty() && authorizeUrl != m_lastAuthorizeUrl)
			{
				m_lastAuthorizeUrl = authorizeUrl;
				emit accountAuthorizeRequested(authorizeUrl);
				m_accountLoginHint = true;
				emit accountLoginHintChanged();
			}
			m_account.insert(QStringLiteral("attemptId"),
				attempt.value(QStringLiteral("id")).toString());
			if (!signedIn)
			{
				m_accountBusy = false;   // 这条分支后面没有并发的 profile/balance，busy 必须在这里收
				m_account.insert(QStringLiteral("busy"), false);
				m_account.insert(QStringLiteral("name"), QString());
				m_account.insert(QStringLiteral("contact"), QString());
				m_account.insert(QStringLiteral("balances"), QVariantList());
				m_account.insert(QStringLiteral("balanceText"), QString());
				emit accountChanged();
				return;
			}

			// profile / balance 两条 RPC 并发，都回来才收起 busy。
			// pending 用 shared_ptr 共享计数：两个回调可能同批到达，捕获的必须是同一份计数。
			const auto pending = std::make_shared<int>(2);
			auto finishOne = [this, pending]() {
				if (--(*pending) > 0)
					return;
				m_accountBusy = false;
				m_account.insert(QStringLiteral("busy"), false);
				emit accountChanged();
			};
			m_api->callMethod(QStringLiteral("account/getProfile"), accountClientArgs(),
				[this, finishOne](const QJsonObject& profileResult) {
					const QString status = profileResult.value(QStringLiteral("status")).toString();
					const QJsonObject profile = profileResult.value(QStringLiteral("value")).toObject();
					// 只有 status=="ready" 才带 value；未就绪时保留旧值，别把姓名清成空串。
					if (status == QLatin1String("ready"))
					{
						m_account.insert(QStringLiteral("name"),
							profile.value(QStringLiteral("name")).toString());   // profile 就三个展示字段：姓名 / 联系方式 / 头像
						m_account.insert(QStringLiteral("contact"),
							profile.value(QStringLiteral("contact")).toString());
						m_account.insert(QStringLiteral("avatarUrl"),
							profile.value(QStringLiteral("avatarUrl")).toString());
					}
					finishOne();
				},
				[this, finishOne](const DshApiClient::RpcError& error) {
					m_account.insert(QStringLiteral("error"),
						QStringLiteral("profile: %1").arg(error.message));
					finishOne();
				});
			m_api->callMethod(QStringLiteral("account/getBalance"), accountClientArgs(),
				[this, finishOne](const QJsonObject& balanceResult) {
					if (balanceResult.value(QStringLiteral("status")).toString()
						== QLatin1String("ready"))
					{
						const QJsonArray wallets = balanceResult.value(QStringLiteral("value")).toArray();
						const QJsonArray bonus = balanceResult.value(QStringLiteral("bonusWallets")).toArray();
						// 余额分两桶：value 是充值钱包、bonusWallets 是赠送钱包；展示合并，"bonus" 标记供界面区分。
						QVariantList rows;
						for (const QJsonValue& value : wallets)
							rows.append(value.toObject().toVariantMap());
						for (const QJsonValue& value : bonus)
						{
							QVariantMap row = value.toObject().toVariantMap();
							row.insert(QStringLiteral("bonus"), true);
							rows.append(row);
						}
						m_account.insert(QStringLiteral("balances"), rows);
						m_account.insert(QStringLiteral("balanceText"), walletText(wallets));
						m_account.insert(QStringLiteral("bonusText"), walletText(bonus));
					}
					finishOne();
				},
				[this, finishOne](const DshApiClient::RpcError& error) {
					m_account.insert(QStringLiteral("error"),
						QStringLiteral("balance: %1").arg(error.message));
					finishOne();
				});
		},
		[this](const DshApiClient::RpcError& error) {
			m_accountBusy = false;
			m_account.insert(QStringLiteral("busy"), false);
			m_account.insert(QStringLiteral("error"), error.message);
			emit accountChanged();
		});
}

void SettingsController::startAccountSignIn()
{
	if (!m_api || m_accountBusy)
		return;
	m_accountBusy = true;
	m_account.insert(QStringLiteral("busy"), true);
	m_account.insert(QStringLiteral("error"), QString());
	emit accountChanged();
	// startSignIn 只负责建 attempt 并返回 authorizeUrl；真正的登录结果靠之后的 getState 轮询。
	QJsonObject args = accountClientArgs();
	args.insert(QStringLiteral("callbackOrigin"), m_api->baseUrl().toString());   // 浏览器登录完回跳到本机 DSH 地址
	args.insert(QStringLiteral("loginSource"), QStringLiteral("desktop"));       // 服务端据此选登录页形态（desktop / web）
	m_api->callMethod(QStringLiteral("account/startSignIn"), args,
		[this](const QJsonObject& value) {
			m_accountBusy = false;
			m_account.insert(QStringLiteral("busy"), false);
			// 返回的 attempt 三字段各有用途：phase 决定按钮态、authorizeUrl 用来开浏览器、id 用于取消。
			const QJsonObject attempt = value.value(QStringLiteral("attempt")).toObject();
			m_account.insert(QStringLiteral("attemptPhase"),
				attempt.value(QStringLiteral("phase")).toString());
			const QString authorizeUrl = attempt.value(QStringLiteral("authorizeUrl")).toString();
			m_account.insert(QStringLiteral("authorizeUrl"), authorizeUrl);
			m_account.insert(QStringLiteral("attemptId"),
				attempt.value(QStringLiteral("id")).toString());
			// 同一 attempt 的 authorizeUrl 只开一次浏览器：与 refreshAccount() 共用 m_lastAuthorizeUrl 去重。
			if (!authorizeUrl.isEmpty() && authorizeUrl != m_lastAuthorizeUrl)
			{
				m_lastAuthorizeUrl = authorizeUrl;
				emit accountAuthorizeRequested(authorizeUrl);
				m_accountLoginHint = true;
				emit accountLoginHintChanged();
			}
			emit accountChanged();
			// 没拿到 authorizeUrl 说明 attempt 还没就绪：600ms 后再问一次，别让界面空等。
			if (authorizeUrl.isEmpty())
				QTimer::singleShot(600, this, [this]() { refreshAccount(); });
		},
		[this](const DshApiClient::RpcError& error) {
			m_accountBusy = false;
			m_account.insert(QStringLiteral("busy"), false);
			m_account.insert(QStringLiteral("error"), error.message);
			emit accountChanged();
		});
}

void SettingsController::cancelAccountSignIn()
{
	if (!m_api)
		return;
	// attemptId 来自上一次 getState/startSignIn：为空说明本来就没有进行中的登录，省掉一次必然失败的 RPC。
	const QString attemptId = m_account.value(QStringLiteral("attemptId")).toString();
	if (attemptId.isEmpty())
		return;
	QJsonObject args;
	args.insert(QStringLiteral("attemptId"), attemptId);
	m_api->callMethod(QStringLiteral("account/cancelSignIn"), args,
		[this](const QJsonObject& value) {
			m_account.insert(QStringLiteral("status"), value.value(QStringLiteral("status")).toString());
			m_account.insert(QStringLiteral("attemptPhase"),
				value.value(QStringLiteral("attempt")).toObject().value(QStringLiteral("phase")).toString());   // 只回 phase：取消后 attempt 已作废，其余字段无意义
			emit accountChanged();
		},
		[this](const DshApiClient::RpcError& error) {
			m_account.insert(QStringLiteral("error"), error.message);
			emit accountChanged();
		});
}

void SettingsController::signOutAccount()
{
	if (!m_api || m_accountBusy)
		return;
	m_accountBusy = true;
	m_account.insert(QStringLiteral("busy"), true);
	m_account.insert(QStringLiteral("error"), QString());
	emit accountChanged();
	// 登出后本地资料与余额一起清空：服务端只回状态，不保证再下发这些字段。
	m_api->callMethod(QStringLiteral("account/signOut"), accountClientArgs(),
		[this](const QJsonObject& value) {
			m_accountBusy = false;
			m_account.insert(QStringLiteral("busy"), false);
			m_account.insert(QStringLiteral("signedIn"),
				value.value(QStringLiteral("status")).toString() == QLatin1String("credential-stored"));
			m_account.insert(QStringLiteral("status"), value.value(QStringLiteral("status")).toString());
			if (m_accountLoginHint)
			{
				m_accountLoginHint = false;
				emit accountLoginHintChanged();
			}
			m_account.insert(QStringLiteral("name"), QString());
			m_account.insert(QStringLiteral("contact"), QString());
			m_account.insert(QStringLiteral("balances"), QVariantList());
			m_account.insert(QStringLiteral("balanceText"), QString());
			emit accountChanged();
		},
		[this](const DshApiClient::RpcError& error) {
			m_accountBusy = false;
			m_account.insert(QStringLiteral("busy"), false);
			m_account.insert(QStringLiteral("error"), error.message);
			emit accountChanged();
		});
}

// 三个外链按钮（用量 / 充值 / 授权）共用服务端下发的 URL：为空时静默不打开，离线也不报错。
void SettingsController::openAccountUsage()
{
	const QString url = m_account.value(QStringLiteral("usageUrl")).toString();
	if (!url.isEmpty())
		QDesktopServices::openUrl(QUrl(url));
}

void SettingsController::openAccountTopUp()
{
	const QString url = m_account.value(QStringLiteral("topUpUrl")).toString();
	if (!url.isEmpty())
		QDesktopServices::openUrl(QUrl(url));
}

void SettingsController::openAccountAuthorize()
{
	const QString url = m_account.value(QStringLiteral("authorizeUrl")).toString();
	if (!url.isEmpty())
		QDesktopServices::openUrl(QUrl(url));
}

void SettingsController::setNotice(const QString& text)
{
	m_notice = text;
	emit noticeChanged();
	if (!text.isEmpty())   // 计时从最后一次提示重算：连续提示只保留最新的 4 秒窗口
		m_noticeTimer->start();
}

void SettingsController::openPage()
{
	// 只在 false→true 时拉数据：重复 open 不触发整页刷新。
	if (m_open)
		return;

	m_open = true;
	emit openChanged();
	refresh();
}

void SettingsController::setPage(int page)
{
	const int bounded = qBound(0, page, 6);   // 页索引 0..6 与 QML 侧 7 个设置页一一对应，越界直接夹住
	if (m_page == bounded)
		return;

	m_page = bounded;
	emit pageChanged();
}

void SettingsController::closePage()
{
	if (!m_open)
		return;

	m_open = false;
	// 丢弃"发现模型"结果：候选列表只对本次打开有效，重开时重新发现。
	m_discovered.clear();
	m_candidates.clear();
	emit openChanged();
	emit candidatesChanged();
}

void SettingsController::removeModel(const QString& provider, const QString& modelId)
{
	const ConfigurableProvider* entry = findProvider(provider);
	const SettingsNamespace* ns = entry ? findNamespace(*entry) : nullptr;
	if (!m_api || !entry || !ns) {
		setStatus(qtTrId("settings_provider_namespace_missing_fmt").arg(provider));
		return;
	}

	setBusy(true);
	// modelId 按值捕获进两个回调：refresh() 之后 *entry 不再有效，但文案里仍要用原 id。
	ModelSelectionService::removeModel(m_api, *entry, *ns, modelId,
		[this, modelId](const SettingsNamespace&) {
			setBusy(false);
			setNotice(qtTrId("model_deleted_fmt").arg(modelId));
			refresh();
		},
		[this, modelId](const DshApiClient::RpcError& error) {
			setBusy(false);
			setNotice(qtTrId("model_delete_failed_fmt").arg(modelId, error.code, error.message));
		});
}

void SettingsController::loadAgentPresets()
{
	if (!m_api)
		return;

	m_presetFeedback.clear();
	// 预设列表与选中项都以服务端为准（每行的 isDefault），本类不落本地副本。
	AgentPresetService::fetch(m_api,
		[this](const QVector<AgentPreset>& presets) {
			QVariantList rows;
			for (const AgentPreset& preset : presets) {
				QVariantMap row;
				row.insert(QStringLiteral("id"), preset.id);
				row.insert(QStringLiteral("name"), preset.name);   // 预设行只带 id/name：界面只做展示与选择
				rows.append(row);
			}
			m_agentPresets = rows;

			if (rows.isEmpty()) {   // 服务端一个预设都没给：按钮上直接写明，不留空白
				m_defaultPresetName = qtTrId("settings_no_preset");
			}
			else {
				const QString selectedId = AgentPresetService::resolveSelectedId(presets, QString());   // 选中项完全由服务端定（每行的 isDefault）；客户端不存本地副本
				m_defaultPresetName.clear();
				for (const AgentPreset& preset : presets) {
					if (preset.id == selectedId) {
						m_defaultPresetName = preset.name;
						break;
					}
				}
				if (m_defaultPresetName.isEmpty())
					m_defaultPresetName = presets.first().name;
			}
			emit presetsChanged();
		},
		[this](const DshApiClient::RpcError&) {
			m_agentPresets.clear();

			// 当前 ULin Hub 的 minimal API profile 没有装 agent preset registry（/api/agentPresets/list 会 404）。
			// 这不是网络故障，不要把底层 transport 文案原样怼到设置页按钮上；明确显示"未内置"。
			m_defaultPresetName = qtTrId("settings_agent_preset_unavailable");
			m_presetFeedback.clear();
			emit presetsChanged();
		});
}

void SettingsController::selectDefaultPreset(const QString& presetId)
{
	if (!m_api || presetId.isEmpty())
		return;

	const QString previousName = m_defaultPresetName;   // 乐观显示：写入失败再退回原文字
	for (const QVariant& value : m_agentPresets) {
		const QVariantMap row = value.toMap();
		if (row.value(QStringLiteral("id")).toString() == presetId) {
			m_defaultPresetName = row.value(QStringLiteral("name")).toString();
			break;
		}
	}
	m_presetFeedback.clear();
	emit presetsChanged();

	AgentPresetService::persistDefault(m_api, presetId,
		[this](const QString& savedId) {
			qInfo().noquote() << "[Settings] default agent preset ->" << savedId;   // 只在成功时留一行日志：界面是乐观更新，没有"已保存"文案
			m_presetFeedback.clear();
			emit presetsChanged();
		},
		[this, previousName](const DshApiClient::RpcError& error) {
			m_defaultPresetName = previousName;
			m_presetFeedback = qtTrId("common_load_failed_fmt").arg(error.code, error.message);
			emit presetsChanged();
		});
}

QObject* SettingsController::ulincore() const
{
		auto& settings = UlinCore::UlinCoreSettings::instance();
	settings.setDataDir(QCoreApplication::applicationDirPath() + QStringLiteral("/data/ulincore"));   // 数据目录跟着 exe 走：便携版与安装版都以可执行文件位置为准
	// 单例由 C++ 持有：QML 不能接管所有权，否则引擎销毁时会 delete 静态对象。
	QQmlEngine::setObjectOwnership(&settings, QQmlEngine::CppOwnership);
	return &settings;
}

QVariantList SettingsController::masterBackends() const
{
	QVariantList rows;
	// dsh 是内置后端，恒占第 0 项；扩展注册的后端按 id 去重后追加在后面。
	// 列表每次现算、不缓存：扩展随时可能装载或卸载。
	QVariantMap dsh;
	dsh.insert(QStringLiteral("id"), QStringLiteral("dsh"));
	dsh.insert(QStringLiteral("name"), QStringLiteral("DSH"));
	dsh.insert(QStringLiteral("description"), QStringLiteral("内置 Agent 后端"));
	rows.append(dsh);

	if (m_api) {
		for (const QVariant& value : m_api->registeredMasterAgents()) {
			const QVariantMap row = value.toMap();
			const QString id = row.value(QStringLiteral("id")).toString();
			if (id.isEmpty() || id == QLatin1String("dsh"))
				continue;
			bool exists = false;   // 扩展可能重复注册同一后端：按 id 去重，避免下拉出现两项同名的
			for (const QVariant& existing : rows) {
				if (existing.toMap().value(QStringLiteral("id")).toString() == id) {
					exists = true;
					break;
				}
			}
			if (!exists)
				rows.append(row);
		}
	}
	return rows;
}

QString SettingsController::masterBackendName() const
{
	for (const QVariant& value : masterBackends()) {
		const QVariantMap row = value.toMap();
		if (row.value(QStringLiteral("id")).toString() == m_masterBackend)
			return row.value(QStringLiteral("name")).toString();
	}
	return m_masterBackend;   // 后端已卸载时退回原始 id，至少不会显示空白
}

void SettingsController::loadMasterBackend()
{
	// 以 api 侧保存的选择为准；没有 api 时退回内置 dsh。值没变就不发信号，避免重复刷新。
	const QString saved = m_api ? m_api->selectedMasterAgentId() : QStringLiteral("dsh");
	if (m_masterBackend == saved)
		return;
	m_masterBackend = saved;
	emit masterBackendChanged();
}

void SettingsController::refreshMasterBackends()
{
	// 扩展注册/卸载后由宿主调用：后端列表与显示名都变了，让 QML 重新取一遍。
	emit masterBackendChanged();
}

void SettingsController::selectMasterBackend(const QString& backendId)
{
	const QString id = backendId.trimmed();
	if (id.isEmpty() || id == m_masterBackend)
		return;
	bool known = false;
	for (const QVariant& value : masterBackends()) {
		if (value.toMap().value(QStringLiteral("id")).toString() == id) {
			known = true;
			break;
		}
	}
	if (!known) {
		emit masterBackendSwitchFailed(id, QStringLiteral("后端未注册"));
		return;
	}
	// 先落盘再激活：激活过程中扩展可能重新注册并触发 QmlBridge 的自动激活，
	// 若仍读到旧值会把刚切走的后端又拉起来。
	if (m_api) m_api->setSelectedMasterAgentId(id);
	if (m_api) {
		// 内置 dsh 恒可激活，故不查询返回值（逗号表达式只为给 ok 一个值）。
		const bool ok = id == QLatin1String("dsh")
			? (m_api->ActivateMasterAgent("dsh"), true)
			: m_api->ActivateMasterAgent(id.toUtf8().constData());
		if (!ok) {
			if (m_api) m_api->setSelectedMasterAgentId(m_masterBackend);   // 回滚落盘值：别让下次启动再去激活一个起不来的后端
			emit masterBackendSwitchFailed(id, QStringLiteral("激活失败"));
			return;
		}
	}
	m_masterBackend = id;
	// 切后端时关掉已打开的宿主设置弹窗：codex / workbuddy 在激活时接管设置入口，
	// 重新点开才会挂上新后端的面板；否则旧弹窗会一直停在那里，看着像"没切换"。
	m_switchingBackend = true;
	emit switchingBackendChanged();
	emit masterSwitchStarting(id);
	closePage();
	m_switchingBackend = false;
	emit switchingBackendChanged();
	setPage(0);   // 切后端后回到第一页：ulincore 模式下就是 ULin Core 设置页
	emit masterBackendChanged();
	emit masterBackendChangeRequested(id);
}

void SettingsController::saveServerUrl(const QString& url)
{
	SettingsStore::setServerUrl(url.trimmed());
	m_serverUrl = SettingsStore::serverUrl();   // 回读落盘值：界面显示的永远是实际保存的内容，而不是输入框原文
	emit serverUrlChanged();
	emit serverRestartRequested();   // Settings::serverSettingsSaved -> ServerManager::restart
}

void SettingsController::loadAppearance()
{
	// 构造函数与 refresh() 都会走到这里：重建列表并重读已保存的语言码（可能已被外部改过）。
	QVariantList rows;
	QVariantMap follow;   // 第 0 项固定是"跟随系统"（数据为空串），保持一致
	follow.insert(QStringLiteral("name"), qtTrId("settings_language_follow_system"));
	follow.insert(QStringLiteral("code"), QString());
	rows.append(follow);

	for (const LanguageInfo& language : Translation::availableLanguages()) {
		QVariantMap row;
		row.insert(QStringLiteral("name"), language.name);
		row.insert(QStringLiteral("code"), language.code);
		rows.append(row);
	}

	m_languages = rows;
	m_languageCode = Translation::savedLanguageCode();
	m_appearanceFeedback.clear();
	emit languageChanged();
}

void SettingsController::setLanguage(const QString& code)
{
	// 先落盘再应用：apply 失败也不回滚已保存的语言码，界面只提示语言包缺失。
	Translation::setSavedLanguageCode(code);
	m_languageCode = code;

	if (Translation::apply(code)) {
		m_appearanceFeedback = qtTrId("settings_language_switched");
	}
	else {
		m_appearanceFeedback = qtTrId("settings_language_pack_missing_fmt")   // 语言包缺失：退回原文并把原因说清楚（不是错误）
			.arg(code.isEmpty() ? qtTrId("settings_language_system_name") : code);
	}
	emit languageChanged();
}

void SettingsController::resetStyles()
{
	ThemeManager::instance().resetStylePack();

	// 风格包重置后让 QML 侧热重载 token：本类在 common（领域层），**不许**直接调 qmlbridge 的
	// ThemeBridge，于是发信号、由 qmlbridge/QmlBridge 接住落地（等价于在这里直接调
	// ThemeBridge::reload()；M8 起 ThemeManager 不再发信号，这条通路只能由本类主动发）。
	emit stylesReset();
	m_appearanceFeedback = qtTrId("settings_styles_reset_done");
	emit languageChanged();
}

// 两者都查最近一次 fetchView 的 m_view：写操作前必须确认 provider 与 namespace 同时存在。
const ConfigurableProvider* SettingsController::findProvider(const QString& provider) const
{
	return m_view.findProvider(provider);
}

const SettingsNamespace* SettingsController::findNamespace(const ConfigurableProvider& provider) const
{
	return m_view.findNamespace(provider.settingsNs);
}

// 三个 setter 都是"变了才发信号"：QML 绑定多，重复 emit 会让整页重算。
void SettingsController::setBusy(bool busy)
{
	if (m_busy == busy)
		return;

	m_busy = busy;
	emit busyChanged();
}

void SettingsController::setStatus(const QString& text)
{
	if (m_status == text)
		return;

	m_status = text;
	emit statusChanged();
}

void SettingsController::setFormFeedback(const QString& text)
{
	if (m_formFeedback == text)
		return;

	m_formFeedback = text;
	emit formFeedbackChanged();
}

QString SettingsController::routeHint(const QString& provider) const
{
	const ConfigurableProvider* entry = findProvider(provider);   // 与现有实现 ModelListPanel::routeHint 逐句一致
	if (!entry)
		return qtTrId("model_no_configurable_provider");

	// 提示由三段拼成：写入路径与条数、该路由的风险提示、settings 只读提示。
	QStringList parts;
	const SettingsNamespace* ns = findNamespace(*entry);
	if (!ns)
		return qtTrId("model_namespace_missing_fmt").arg(entry->settingsNs);

	const QJsonArray models = ModelSelectionService::configuredModels(*ns, entry->settingsPath);
	const bool userDeclared = ModelSelectionService::userDeclaresModels(*ns, entry->settingsPath);

	parts.append(qtTrId("model_write_progress_fmt").arg(entry->settingsNs,
		ModelSelectionService::modelsPath(*entry).join(QLatin1Char('.')),
		QString::number(models.size())));

	if (routeIsPiAi(provider)) {
		if (!userDeclared)   // pi-ai 的 models 是整体替换，会收窄公布范围
			parts.append(qtTrId("model_write_warning_desc"));
	}
	else {
		parts.append(qtTrId("model_write_deepseek_warning"));
	}

	if (!m_view.settingsWritable)
		parts.append(qtTrId("model_write_readonly"));

	return parts.join(QLatin1Char(' '));
}

bool SettingsController::routeIsPiAi(const QString& provider) const
{
	// 判据是 settings namespace 名里带 pi-ai：pi-ai 系路由的条目形态与写回语义都和 DeepSeek 不同。
	const ConfigurableProvider* entry = findProvider(provider);
	return entry && entry->settingsNs.contains(QStringLiteral("pi-ai"));
}

void SettingsController::writeModel(const ConfigurableProvider& provider, const SettingsNamespace& ns, const AddModelRequest& request, bool replacing)
{
	setFormFeedback(qtTrId("model_writing"));
	// 成功文案按"覆盖已有条目 / 新增条目"区分：replacing 由 submitModelForm 事先判定。
	ModelSelectionService::addModel(m_api, provider, ns, request,
		[this, request, replacing](const SettingsNamespace&) {   // 参数是写入后的快照，用不上：统一 refresh() 重拉全量
			setBusy(false);
			setFormFeedback(replacing
				? qtTrId("model_updated_fmt").arg(request.id)
				: qtTrId("model_added_fmt").arg(request.id));
			emit modelAdded(request.provider, request.id);
			refresh();
		},
		[this](const DshApiClient::RpcError& error) {
			setBusy(false);
			setFormFeedback(qtTrId("model_add_failed_fmt").arg(error.code, error.message));
		});
}

void SettingsController::submitModelForm(const QString& provider, const QString& modelId, const QString& displayName, const QString& contextWindow, const QString& maxTokens, const QString& efforts, const QString& apiKey)
{
	if (provider.isEmpty()) {   // 校验顺序与文案沿用（ModelListPanel::submitForm）
		setFormFeedback(qtTrId("model_select_provider_first"));
		return;
	}
	if (modelId.trimmed().isEmpty()) {
		setFormFeedback(qtTrId("model_id_required"));
		return;
	}

	const ConfigurableProvider* entry = findProvider(provider);
	const SettingsNamespace* ns = entry ? findNamespace(*entry) : nullptr;
	if (!entry || !ns) {
		setFormFeedback(qtTrId("model_namespace_missing_fmt").arg(provider));
		return;
	}

	AddModelRequest request;
	request.provider = entry->provider;
	request.id = modelId.trimmed();
	request.name = displayName.trimmed();

	const auto parseOptional = [](const QString& text, bool* has, int* value) {   // 可选整数：空 = 不写；非法 = 拒绝
		const QString trimmed = text.trimmed();
		if (trimmed.isEmpty()) {
			*has = false;
			return true;
		}
		bool ok = false;
		const int parsed = trimmed.toInt(&ok);
		if (!ok || parsed <= 0)
			return false;
		*has = true;
		*value = parsed;
		return true;
		};

	if (!parseOptional(contextWindow, &request.hasContextWindow, &request.contextWindow)
		|| !parseOptional(maxTokens, &request.hasMaxTokens, &request.maxTokens)) {
		setFormFeedback(qtTrId("model_numeric_invalid"));
		return;
	}

	const bool piAi = routeIsPiAi(provider);
	// pi-ai 的 reasoningEfforts 只认目录公布过的 level id：写错会被服务端静默忽略，所以在提交前就拒掉。
	if (piAi && !efforts.trimmed().isEmpty()) {
		QStringList available;
		for (const ReasoningLevel& level : fallbackReasoningLevels())
			available << level.id;

		const QStringList typed = efforts.split(QLatin1Char(','), Qt::SkipEmptyParts);
		for (const QString& raw : typed) {
			const QString id = raw.trimmed();
			if (id.isEmpty())
				continue;
			if (!available.contains(id)) {
				setFormFeedback(qtTrId("model_think_level_unknown_fmt").arg(id, available.join(QLatin1Char('/'))));
				return;
			}
			request.reasoningEfforts.append(id);
		}
	}

	// 凭据引用：profile 点名的优先，否则按约定派生并记入配置
	const QString declared = ModelSelectionService::profileApiKeyEnv(*ns, entry->settingsPath);
	const QString ref = declared.isEmpty()
		? ModelSelectionService::deriveKeyRef(entry->provider)
		: declared;

	bool replacing = false;   // 同 ID 已存在 = 覆盖
	const QJsonArray existing = ModelSelectionService::configuredModels(*ns, entry->settingsPath);
	for (const QJsonValue& value : existing) {
		if (value.toObject().value(QStringLiteral("id")).toString() == request.id) {
			replacing = true;
			break;
		}
	}

	setBusy(true);
	const QString typedKey = apiKey.trimmed();
	// 两条写路径：没填 key 直接写条目；填了就先写凭据再写条目，避免条目已落地而 key 缺失。
	if (typedKey.isEmpty()) {
		writeModel(*entry, *ns, request, replacing);
		return;
	}

	request.apiKeyRef = ref;
	request.recordApiKeyEnv = declared.isEmpty() && !ref.isEmpty();
	setFormFeedback(qtTrId("model_writing_credential"));
	// 按值拷贝 provider/namespace：*entry、*ns 指向 m_view，凭据回调回来前 refresh() 可能已换掉它。
	const ConfigurableProvider providerCopy = *entry;
	const SettingsNamespace nsCopy = *ns;
	ModelSelectionService::saveCredential(m_api, ref, typedKey,
		[this, providerCopy, nsCopy, request, replacing]() {
			writeModel(providerCopy, nsCopy, request, replacing);
		},
		[this, ref](const DshApiClient::RpcError& error) {
			setBusy(false);
			setFormFeedback(qtTrId("model_credential_write_failed_fmt").arg(ref, error.code, error.message));
		});
}

void SettingsController::createCustomProvider(const QString& route, const QString& displayName, const QString& baseUrl, const QString& api, const QString& modelId, const QString& modelName, const QString& apiKey)
{
	const QString trimmedRoute = route.trimmed();
	// 路由 ID 会写进 settings 与凭据引用：限定小写字母开头、短横线分段，避免与内置路由撞名或写出非法键。
	static const QRegularExpression routePattern(QStringLiteral("^[a-z][a-z0-9]*(?:-[a-z0-9]+)*$"));
	if (!routePattern.match(trimmedRoute).hasMatch()) {
		setFormFeedback(QStringLiteral("提供商 ID 需以小写字母开头，只能包含小写字母、数字和短横线。"));
		return;
	}
	for (const QVariant& value : m_providers) {
		if (value.toMap().value(QStringLiteral("provider")).toString() == trimmedRoute) {
			setFormFeedback(QStringLiteral("已有提供商使用了这个 ID。"));
			return;
		}
	}
	const QString trimmedUrl = baseUrl.trimmed();
	if (trimmedUrl.isEmpty() || (!trimmedUrl.startsWith(QStringLiteral("http://"))
			&& !trimmedUrl.startsWith(QStringLiteral("https://")))) {
		setFormFeedback(QStringLiteral("请填写有效的 http(s) API 地址。"));
		return;
	}
	if (api.trimmed().isEmpty() || modelId.trimmed().isEmpty()) {
		setFormFeedback(QStringLiteral("请选择 API 协议并至少填写一个模型 ID。"));
		return;
	}
	const SettingsNamespace* ns = m_view.findNamespace(QStringLiteral("llm-pi-ai"));
	// 自定义提供商只能落在 llm-pi-ai 这个 namespace 下；没挂载就无从写入。
	if (!ns) {
		setFormFeedback(QStringLiteral("当前 DSH 没有挂载 llm-pi-ai，无法新增自定义提供商。"));
		return;
	}

	const QString key = apiKey.trimmed();
	ModelSelectionService::CustomProviderRequest request;
	request.route = trimmedRoute;
	request.displayName = displayName.trimmed();
	request.baseURL = trimmedUrl;
	request.api = api.trimmed();
	request.modelId = modelId.trimmed();
	request.modelName = modelName.trimmed();
	request.apiKeyRef = key.isEmpty() ? QString() : ModelSelectionService::providerKeyRef(trimmedRoute);   // 没填 key 就只建 provider，凭据留给后续补

	setBusy(true);
	setFormFeedback(QStringLiteral("正在保存提供商…"));
	const SettingsNamespace nsCopy = *ns;   // 同 submitModelForm：ns 指向 m_view，回调期间可能失效
	// 顺序固定：先建 provider 条目（失败即整体失败），再写凭据（失败只降级为"已保存但 key 未写入"）。
	ModelSelectionService::createCustomProvider(m_api, nsCopy, request,
		[this, request, key, nsCopy](const SettingsNamespace&) {
			setFormFeedback(QStringLiteral("提供商已保存：%1").arg(request.route));
			if (!key.isEmpty()) {
				ModelSelectionService::saveCredential(m_api, request.apiKeyRef, key,
					[this]() { setBusy(false); refresh(); },
					[this](const DshApiClient::RpcError& error) {
						setBusy(false);
						setFormFeedback(QStringLiteral("提供商已保存，但 API key 写入失败：%1").arg(error.message));
					});
				return;
			}
			setBusy(false);
			refresh();
		},
		[this](const DshApiClient::RpcError& error) {
			setBusy(false);
			setFormFeedback(QStringLiteral("保存提供商失败：%1 %2").arg(error.code, error.message));
		});
}

// 打开设置页或改动后重拉：服务端地址、预设、模型目录与凭据状态。
void SettingsController::refresh()
{
	if (!m_api)
		return;

	setBusy(true);
	setStatus(QString());   // 清掉上一次的状态行，旧的失败文案不该留在新一次读取里

	loadAgentPresets();   // 同一行为 Settings::refreshOnOpen：每次打开重同步服务端地址、预设、模型目录
	loadMasterBackend();
	loadAppearance();
	m_serverUrl = SettingsStore::serverUrl();
	// 本地还没存过地址时回落到 api 实际连的地址：首次运行的设置页也要有值可显示。
	if (m_serverUrl.isEmpty())
		m_serverUrl = m_api->baseUrl().toString();
	emit serverUrlChanged();

	// fetchView 一次拿全 provider 目录、settings 快照与失败项，再交给 rebuildProviders() 摊平成 QML 行。
	ModelSelectionService::fetchView(m_api,
		[this](const ServerModelView& view) {
			m_view = view;   // 整份替换：m_view 只代表最近一次 fetchView 的结果
			setBusy(false);
			rebuildProviders();

			m_credentials.clear();  // 逐个 provider 查凭据状态：配没配、可不可写、来源（env / file）
			// 凭据查询是逐 provider 的异步扇出：每回来一条就 rebuild 一次，界面渐进补全；
			// 这里不等全部完成，busy 只跟 fetchView 绑定。
			for (const ConfigurableProvider& provider : m_view.providers) {
				// deepseek-account 走账号登录，不需要 API Key（服务端 hasProviderApiKey 也跳过它）；
				// 别去 describe 一个不存在的凭据引用，否则设置页会显示「缺 key」。
				if (provider.provider == QLatin1String("deepseek-account"))
					continue;

				const SettingsNamespace* ns = findNamespace(provider);
				if (!ns)
					continue;

				const QString ref = ModelSelectionService::resolveKeyRef(provider.provider, *ns, provider.settingsPath);
				// ref 为空 = 这个 provider 没有可描述的凭据引用，直接跳过，别发一条必然失败的 describe。
				if (ref.isEmpty())
					continue;

				ModelSelectionService::describeCredential(m_api, ref,
					[this](const CredentialStatus& status) {
						m_credentials.append(status);
						rebuildProviders();
					},
					[](const DshApiClient::RpcError& error) {
						// 单条凭据查不到只记日志：其它 provider 照常渲染，不把整页打成错误态。
						qWarning().noquote() << "[Settings] credentials/describe 失败:" << error.code << error.message;
					});
			}

			if (m_view.providers.isEmpty())   // 一个 provider 都没有时给条状态，否则模型页整片空白且无解释
				setStatus(qtTrId("model_provider_none_reported"));
		},
		[this](const DshApiClient::RpcError& error) {
			setBusy(false);
			setStatus(qtTrId("settings_read_failed_fmt").arg(error.code, error.message));
		});
}

// 把 provider、凭据状态、模型行与失败项摊平成 QML 直接可用的 QVariantList。
// provider 行分三组字段：目录信息、凭据状态（keyKnown/keyConfigured/keyWritable/keySource）、已声明的模型列表。
void SettingsController::rebuildProviders()
{
	QVariantList rows;
	for (const ConfigurableProvider& provider : m_view.providers) {
		const SettingsNamespace* ns = findNamespace(provider);

		QVariantMap row;
		row.insert(QStringLiteral("provider"), provider.provider);
		row.insert(QStringLiteral("displayName"), provider.displayName);
		row.insert(QStringLiteral("settingsNs"), provider.settingsNs);
		row.insert(QStringLiteral("active"), provider.active);
		row.insert(QStringLiteral("declared"), provider.declared);

		QString label = provider.displayName;   // 下拉项文案：与现有实现 syncFormToRoute 同规则（displayName + 路由后缀 + 未启用后缀）
		if (label != provider.provider)
			label += qtTrId("model_provider_route_suffix_fmt").arg(provider.provider);
		if (!provider.active)
			label += qtTrId("model_disabled_suffix");
		row.insert(QStringLiteral("label"), label);

		QString ref;
		bool declaredByProfile = false;   // key 引用是 profile 写死的、还是按 provider 约定派生：QML 用来决定要不要让用户编辑
		if (ns) {
			declaredByProfile = !ModelSelectionService::profileApiKeyEnv(*ns, provider.settingsPath).isEmpty();
			ref = ModelSelectionService::resolveKeyRef(provider.provider, *ns, provider.settingsPath);
		}
		// deepseek-account 走账号登录，不需要 API Key（与服务端 hasProviderApiKey 的跳过一致）。
		const bool noApiKey = provider.provider == QLatin1String("deepseek-account");
		row.insert(QStringLiteral("keyRef"), ref);
		row.insert(QStringLiteral("keyDeclaredByProfile"), declaredByProfile);
		row.insert(QStringLiteral("namespaceFound"), ns != nullptr);
		row.insert(QStringLiteral("noApiKey"), noApiKey);

		bool known = false;   // 凭据状态：还没查到时按"未知"呈现（不能当成只读）
		bool configured = false;
		bool writable = false;
		QString source;
		if (noApiKey) {
			// 账号登录视作已就绪：没有 key 可填，也不该提示「缺 key」。
			known = true;
			configured = true;
			writable = false;
			source = QStringLiteral("account");
		}
		else {
			for (const CredentialStatus& status : m_credentials) {
				if (status.ref != ref || ref.isEmpty())
					continue;
				known = status.known;
				configured = status.configured;
				writable = status.writable;
				source = status.source;
				break;
			}
		}
		row.insert(QStringLiteral("keyKnown"), known);
		row.insert(QStringLiteral("keyConfigured"), configured);
		row.insert(QStringLiteral("keyWritable"), writable);
		row.insert(QStringLiteral("keySource"), source);

		QVariantList models;  // 已声明的模型（settings 里的 models 数组，整份替换语义）
		if (ns) {
			const QJsonArray declared = ModelSelectionService::configuredModels(*ns, provider.settingsPath);
			for (const QJsonValue& value : declared) {
				ConfiguredModel model;
				if (!ModelSelectionService::parseConfiguredModel(value, &model))
					continue;
				QVariantMap entry;
				entry.insert(QStringLiteral("id"), model.id);
				entry.insert(QStringLiteral("name"), model.name);
				models.append(entry);
			}
		}
		row.insert(QStringLiteral("models"), models);
		rows.append(row);
	}

	m_providers = rows;

	// 模型页的行：与现有实现 ModelListPanel 一样，用 buildModelInfos 把"目录公布的模型"与
	// "settings 里声明的条目"join 成展示行（容量、写回地址只有 settings 侧知道）
	QVariantList modelRows;
	const QVector<ModelInfo> infos = ModelSelectionService::buildModelInfos(m_view);
	for (const ModelInfo& info : infos) {
		QVariantMap row;
		row.insert(QStringLiteral("provider"), info.provider);
		row.insert(QStringLiteral("providerName"), info.providerName);
		row.insert(QStringLiteral("id"), info.id);
		row.insert(QStringLiteral("name"), info.name);
		row.insert(QStringLiteral("description"), info.description);
		row.insert(QStringLiteral("declared"), info.declared);
		row.insert(QStringLiteral("userDeclared"), info.userDeclared);
		row.insert(QStringLiteral("hasContextWindow"), info.hasContextWindow);
		row.insert(QStringLiteral("contextWindow"), info.contextWindow);
		row.insert(QStringLiteral("hasMaxTokens"), info.hasMaxTokens);
		row.insert(QStringLiteral("maxTokens"), info.maxTokens);
		row.insert(QStringLiteral("hasReasoning"), info.hasReasoning);
		row.insert(QStringLiteral("reasoningDisabled"), info.reasoningDisabled);
		row.insert(QStringLiteral("settingsNs"), info.settingsNs);
		row.insert(QStringLiteral("settingsWritable"), info.settingsWritable);

		QStringList levels;
		for (const ReasoningLevel& level : info.levels)
			levels << level.name;
		row.insert(QStringLiteral("levels"), levels);
		modelRows.append(row);
	}
	m_models = modelRows;

	QVariantList failures;  // 失败项（原版状态行会逐个列出：提供方名 + 原因）
	for (const ModelCatalogFailure& failure : m_view.failures) {
		QVariantMap row;
		row.insert(QStringLiteral("id"), failure.id);
		row.insert(QStringLiteral("name"), failure.name);
		row.insert(QStringLiteral("message"), failure.message);
		failures.append(row);
	}
	m_failures = failures;
	m_groupCount = m_view.groups.size();

	emit viewChanged();
}

// 保存单个 provider 的 API key：写完后重拉状态，并通知宿主刷新会话模型目录。
void SettingsController::saveApiKey(const QString& provider, const QString& apiKey)
{
	if (!m_api || apiKey.trimmed().isEmpty()) {
		setStatus(qtTrId("model_api_key_required"));
		return;
	}

	const ConfigurableProvider* entry = findProvider(provider);
	const SettingsNamespace* ns = entry ? findNamespace(*entry) : nullptr;
	if (!entry || !ns) {
		setStatus(qtTrId("settings_provider_namespace_missing_fmt").arg(provider));
		return;
	}

	// ref 是凭据在服务端的引用名：profile 声明优先，否则按 provider 约定派生。
	const QString ref = ModelSelectionService::resolveKeyRef(entry->provider, *ns, entry->settingsPath);
	setBusy(true);
	ModelSelectionService::saveCredential(m_api, ref, apiKey.trimmed(),
		[this, ref]() {
			setBusy(false);
			setStatus(qtTrId("model_credential_written_fmt").arg(ref));
			refresh();  // 重新查状态：界面上的"已配置"要立刻反映出来
			// 凭据写入后，原本因缺 Key 而不出现在会话模型目录里的模型会变得可用；
			// 通知宿主立即重拉会话模型目录，避免必须切会话才看到新模型。
			emit modelCatalogChanged();
		},
		[this](const DshApiClient::RpcError& error) {
			setBusy(false);
			setStatus(qtTrId("model_api_key_write_failed_fmt").arg(error.code, error.message));
		});
}

void SettingsController::discover(const QString& provider, const QString& apiKey)
{
	// 只探测目录、不写 settings：结果进 m_candidates 供用户挑，真正落库走 addModel()。
	const ConfigurableProvider* entry = findProvider(provider);
	const SettingsNamespace* ns = entry ? findNamespace(*entry) : nullptr;
	if (!m_api || !entry || !ns) {
		setStatus(qtTrId("settings_provider_namespace_missing_fmt").arg(provider));
		return;
	}

	setBusy(true);
	// 发现请求同时带上表单里刚输入的 key（可能还没保存），用于临时探测模型目录。
	const QJsonObject request = ModelSelectionService::buildDiscoveryRequest(*entry, ns, apiKey);
	ModelSelectionService::discoverModels(m_api, entry->settingsNs, request,
		[this](const QVector<DiscoveredModel>& models) {
			setBusy(false);
			m_discovered = models;   // 保留完整结果（含容量字段），addModel() 会按 id 回查

			QVariantList rows;
			for (const DiscoveredModel& model : models) {
				QVariantMap row;
				row.insert(QStringLiteral("id"), model.id);
				row.insert(QStringLiteral("name"), model.name.isEmpty() ? model.id : model.name);   // 上游不给显示名时退回 id
				row.insert(QStringLiteral("contextWindow"), model.hasContextWindow ? model.contextWindow : 0);
				rows.append(row);
			}
			m_candidates = rows;
			emit candidatesChanged();
			setStatus(rows.isEmpty()
				? qtTrId("model_fetch_none_found")
				: qtTrId("model_fetch_found_fmt").arg(rows.size()));
		},
		[this](const DshApiClient::RpcError& error) {
			setBusy(false);
			setStatus(qtTrId("model_fetch_failed_fmt").arg(error.code, error.message));   // 失败不清候选：上一批结果仍可选择
		});
}

void SettingsController::addModel(const QString& provider, const QString& modelId, const QString& displayName)
{
	const ConfigurableProvider* entry = findProvider(provider);
	const SettingsNamespace* ns = entry ? findNamespace(*entry) : nullptr;
	if (!m_api || !entry || !ns) {
		setStatus(qtTrId("settings_provider_namespace_missing_fmt").arg(provider));
		return;
	}
	if (modelId.trimmed().isEmpty()) {
		setStatus(qtTrId("model_id_required"));
		return;
	}

	AddModelRequest request;
	request.provider = entry->provider;
	request.id = modelId.trimmed();
	request.name = displayName.trimmed();
	for (const DiscoveredModel& model : m_discovered) {   // 容量等字段留给"发现"回来的数值（addModel 会按 pi-ai / 非 pi-ai 两种形态写条目）
		if (model.id != request.id)
			continue;
		request.hasContextWindow = model.hasContextWindow;
		request.contextWindow = model.contextWindow;
		request.hasMaxTokens = model.hasMaxTokens;
		request.maxTokens = model.maxTokens;
		break;
	}

	setBusy(true);
	ModelSelectionService::addModel(m_api, *entry, *ns, request,
		[this, provider, modelId](const SettingsNamespace&) {
			setBusy(false);
			setStatus(qtTrId("model_added_fmt").arg(modelId));
			emit modelAdded(provider, modelId);   // 宿主据此刷新输入区的模型选择器
			refresh();
		},
		[this](const DshApiClient::RpcError& error) {
			setBusy(false);
			setStatus(qtTrId("model_add_failed_fmt").arg(error.code, error.message));
		});
}
