#include "common/agent/dsh/pluginmarket/PluginMarketModel.h"

#include <QJsonObject>
#include <algorithm>

QString MarketPlugin::displayDescription() const
{
	// 中文描述缺失时退回英文，市场列表始终有可显示文本。
	return !descriptionZh.isEmpty() ? descriptionZh : descriptionEn;
}

bool MarketPlugin::matches(const QString& keyword) const
{
	// 关键字对名称、分类、中英文描述做不区分大小写的包含匹配。
	if (keyword.isEmpty())
		return true;

	return name.contains(keyword, Qt::CaseInsensitive)
		|| category.contains(keyword, Qt::CaseInsensitive)
		|| descriptionZh.contains(keyword, Qt::CaseInsensitive)
		|| descriptionEn.contains(keyword, Qt::CaseInsensitive);
}

namespace PluginMarketModel
{
	QVector<MarketPlugin> parsePlugins(const QJsonArray& registryPlugins)
	{
		QVector<MarketPlugin> plugins;
		plugins.reserve(registryPlugins.size());

		// registry 里没有 name 的条目视为无效，直接跳过。
		for (const auto& value : registryPlugins) {
			const QJsonObject object = value.toObject();

			MarketPlugin plugin;
			plugin.name = object.value(QStringLiteral("name")).toString();
			if (plugin.name.isEmpty())
				continue;

			plugin.category = object.value(QStringLiteral("category")).toString();
			plugin.url = object.value(QStringLiteral("url")).toString();

			const QJsonObject description = object.value(QStringLiteral("description")).toObject();
			plugin.descriptionZh = description.value(QStringLiteral("zh")).toString();
			plugin.descriptionEn = description.value(QStringLiteral("en")).toString();

			plugins.append(plugin);
		}

		return plugins;
	}

	QVector<MarketPlugin> filter(const QVector<MarketPlugin>& plugins, const QString& keyword)
	{
		const QString trimmed = keyword.trimmed();
		if (trimmed.isEmpty())
			return plugins;

		QVector<MarketPlugin> matched;
		for (const MarketPlugin& plugin : plugins) {
			if (plugin.matches(trimmed))
				matched.append(plugin);
		}
		return matched;
	}

	Page paginate(int total, int pageSize, int requestedPage)
	{
		Page page;
		if (pageSize <= 0)
			pageSize = 1;

		page.count = std::max(1, (total + pageSize - 1) / pageSize);  // 向上取整：空列表也算 1 页
		page.index = std::clamp(requestedPage, 0, page.count - 1);
		page.begin = page.index * pageSize;
		page.end = std::min(page.begin + pageSize, total);
		return page;
	}
}