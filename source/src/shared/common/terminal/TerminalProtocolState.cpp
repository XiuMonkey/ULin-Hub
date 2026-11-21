#include "common/terminal/TerminalProtocolState.h"

#include <QtGlobal>

void TerminalProtocolState::beginAttachment()
{
	m_uplinkSequence = 0;
	m_sequence = -1;
	m_awaitSnapshot = true;
	m_pendingBytes = 0;
}

void TerminalProtocolState::markDetached()
{
	m_sequence = -1;
	m_awaitSnapshot = false;
}

TerminalProtocolState::FrameStatus TerminalProtocolState::acceptFrame(int sequence, bool snapshot)
{
	if (snapshot) {
		if (!m_awaitSnapshot)
			return FrameStatus::UnexpectedSnapshot;
		m_awaitSnapshot = false;
		m_sequence = sequence;
		return FrameStatus::Snapshot;
	}

	if (m_awaitSnapshot)
		return FrameStatus::NeedSnapshot;
	if (sequence != m_sequence + 1)
		return FrameStatus::Gap;

	m_sequence = sequence;
	return FrameStatus::Data;
}

void TerminalProtocolState::setLimits(int maxCols, int maxRows, int maxInputBytes)
{
	m_maxCols = qMax(2, maxCols);
	m_maxRows = qMax(1, maxRows);
	m_maxInputBytes = qMax(1024, maxInputBytes);
}

void TerminalProtocolState::requestGrid(int cols, int rows)
{
	m_wantCols = qBound(2, cols, m_maxCols);
	m_wantRows = qBound(1, rows, m_maxRows);
}

bool TerminalProtocolState::takeGridChange(int* cols, int* rows)
{
	if (m_wantCols <= 0 || m_wantRows <= 0)
		return false;
	if (m_wantCols == m_knownCols && m_wantRows == m_knownRows)
		return false;
	m_knownCols = m_wantCols;
	m_knownRows = m_wantRows;
	if (cols)
		*cols = m_knownCols;
	if (rows)
		*rows = m_knownRows;
	return true;
}

void TerminalProtocolState::resetKnownGrid()
{
	m_knownCols = 0;
	m_knownRows = 0;
}

bool TerminalProtocolState::notePendingBytes(qint64 bytes)
{
	m_pendingBytes += qMax<qint64>(0, bytes);
	return m_pendingBytes > m_maxInputBytes;
}

void TerminalProtocolState::releasePendingBytes(qint64 bytes)
{
	m_pendingBytes = qMax<qint64>(0, m_pendingBytes - qMax<qint64>(0, bytes));
}
