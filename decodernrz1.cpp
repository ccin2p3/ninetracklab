#include <QtDebug>
#include <QTime>

#include "ninetracklab.h"
#include "tapedrive.h"
#include "decodernrz1.h"

// --------------------------------------------------------------------------
DecoderNRZ1::DecoderNRZ1()
{
}

// --------------------------------------------------------------------------
quint16 DecoderNRZ1::crc(quint16 *data, int size)
{
	int i = 0;
	quint16 reg = 0;
	int bit;

	while (i < size) {
		reg ^= data[i++];
//again:
		bit = reg & 1;
		reg >>= 1;
		reg |= bit << 8;
		if (bit == 1) {
			reg ^= 0b000111100;
		}

		if ((i == size) && (reg & 0b100000000)) {
			//goto again;
		}
	}

	return reg ^ 0b111010111;
}

// --------------------------------------------------------------------------
int DecoderNRZ1::get_block(TapeDrive &td, BlockStore &bs)
{
	int pulse_start;
	int last_pulse_start = td.get_pos();
	int rowcount = 0;
	int b_crc = -1;
	int d_hparity = 0;
	unsigned block_start = td.get_pos();

	QList<TapeEvent> e;

	while (1) {
		int pulse = td.read(&pulse_start, deskew, edge_sens);
		if (pulse < 0) {
			return pulse;
		}

		const quint16 tape_mark = 0b000010011;

		int time_delta = pulse_start - last_pulse_start;

		// hparity and crc pulse
		if ((rowcount > 1) && (time_delta >= bpl4_min) && (time_delta <= bpl4_max)) {
			// hparity
			if (b_crc != -1) {
				e.append(TapeEvent(pulse_start, C_HPARITY, ""));
				TapeChunk c(F_NRZ1, td.get_pos()-block_start, buf, rowcount, e);
				c.b_hparity = pulse;
				c.d_hparity = d_hparity;
				c.b_crc = b_crc;
				c.d_crc = crc(buf, rowcount);
				bs.addChunk(block_start, c);
				return VT_OK;
			// crc
			} else {
				b_crc = pulse;
				d_hparity ^= pulse;
				e.append(TapeEvent(pulse_start, C_CRC, ""));
			}
		// tape mark
		} else if ((pulse == tape_mark) && (time_delta >= bpl8_min) && (time_delta <= bpl8_max) && (rowcount == 1) && (buf[rowcount-1] == tape_mark)) {
			qDebug() << "tape mark";
			e.append(TapeEvent(pulse_start, C_ROW, ""));
			TapeChunk c(F_NRZ1, td.get_pos()-block_start, e);
			bs.addChunk(block_start, c);
			return VT_OK;
		// data
		} else {
			// start of a new block
			if (rowcount == 0) {
				block_start = pulse_start;
			// discard too long pulses
			} else if ((rowcount == 1) && (time_delta >= bpl4_min)) {
				rowcount = 0;
				d_hparity = 0;
				e.clear();
				block_start = pulse_start;
			}
			d_hparity ^= pulse;
			int vp = !(td.parity9(pulse) ^ (pulse>>8));
			if (vp) {
				e.append(TapeEvent(pulse_start, C_EVPAR, ""));
			} else {
				e.append(TapeEvent(pulse_start, C_ROW, QString((char)pulse&0xff)));
			}
			buf[rowcount] = pulse;
			rowcount++;
		}

		last_pulse_start = pulse_start;

	}

	return VT_OK;
}

// --------------------------------------------------------------------------
int DecoderNRZ1::run(TapeDrive &td, BlockStore &bs)
{
	QTime myTimer;
	myTimer.start();

	td.rewind();
	qDebug() << "deskew:" << deskew << "edge_sens:" << edge_sens << "bpl4:" << bpl4_min << "-" << bpl4_max << "bpl8:" << bpl8_min << "-" << bpl8_max;
	while (get_block(td, bs) == VT_OK) {

	}
	int ms = myTimer.elapsed();
	qDebug() << "nrz loop done in" << ms << "ms";

	return VT_OK;
}

