#include "common/util/ConnectionManager.h"

// 接口方法均内联在 ConnectionManager.h，本文件只留单例、析构与两个前缀规范化助手。
ConnectionManager& ConnectionManager::instance()
{
	static ConnectionManager* const inst = new ConnectionManager();
	return *inst;
}

ConnectionManager::~ConnectionManager() = default;

// Qt 字符串版 connect 靠首字符识别类型：'2' 为信号、'1' 为槽；缺前缀不会报错，只会得到空连接。
QByteArray ConnectionManager::EnsureSignalPrefix(QByteArray signal)
{
	if (!signal.isEmpty() && signal.at(0) != '2')
		signal.prepend('2');
	return signal;
}

QByteArray ConnectionManager::EnsureSlotPrefix(QByteArray slot)
{
	if (!slot.isEmpty() && slot.at(0) != '1' && slot.at(0) != '2')  // 已是 '2' 的不动：信号连信号时槽位本该带 '2'
		slot.prepend('1');
	return slot;
}
