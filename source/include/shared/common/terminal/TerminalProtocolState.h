#pragma once

#include <QtGlobal>

// 终端协议状态：只放可脱离 QWebChannel/QML 的纯规则。
// 帧序、snapshot 代际、上行序号、网格限幅与去重、输入/待投队列的内存护栏。
class TerminalProtocolState
{
public:
	enum class FrameStatus
	{
		Snapshot,
		Data,
		UnexpectedSnapshot,
		NeedSnapshot,
		Gap
	};

	void beginAttachment();
	void markDetached();
	FrameStatus acceptFrame(int sequence, bool snapshot);

	int sequence() const { return m_sequence; }
	bool awaitSnapshot() const { return m_awaitSnapshot; }

	int nextUplinkSequence() { return ++m_uplinkSequence; }

	void setLimits(int maxCols, int maxRows, int maxInputBytes);
	void requestGrid(int cols, int rows);
	int wantedCols() const { return m_wantCols; }
	int wantedRows() const { return m_wantRows; }
	// 第一次或尺寸确实变化时返回 true，并写入 cols/rows。
	bool takeGridChange(int* cols, int* rows);
	void resetKnownGrid();

	bool notePendingBytes(qint64 bytes);
	void releasePendingBytes(qint64 bytes);
	qint64 pendingBytes() const { return m_pendingBytes; }

private:
	int m_sequence = -1;
	bool m_awaitSnapshot = true;
	int m_uplinkSequence = 0;

	int m_maxCols = 500;
	int m_maxRows = 200;
	int m_maxInputBytes = 65536;
	int m_wantCols = 0;
	int m_wantRows = 0;
	int m_knownCols = 0;
	int m_knownRows = 0;
	qint64 m_pendingBytes = 0;
};
