#pragma once


#define _TnbLOG_DISABLE
#define _TnbTraceLogger_LogSize 100 * 1024
#include "TnbRs232c.h"
#include "TnbThread.h"
#ifdef _TnbLOG_DISABLE
	#define TLOG			__noop
#else
	#include "TnbTraceLogger.h"
#endif


/**
 * 8255�N���X
 */
class C8255 : CThread::IRunner
{
public:

	/// �R���X�g���N�^
	C8255(void) : m_isRecvEndMark(false)
	{
	}

	/**
	 * �������i�A�N�e�B�u���j
	 *	@note �����VID,PID��COM�|�[�g�ɑ΂��������R�}���h�𓊂��ĉ������m�F����B
	 *	@param isNotifyMode -1;��̏��̂܂܏������B0;�R�}���h���[�h�ŏ������B1;�ʒm���[�h�ŏ�����
	 *	@param withDialog true�Ȃ�_�C�A���O���o���܂��B
	 *	@retval �����B
	 *	@retval ���s�B
	 */
	bool Init(int isNotifyMode, bool withDialog = true)
	{
		Stop();
		m_isNotifyMode = false;
		CWordVector vp = CRs232c::EnumExistPortsByUsb(0x04d8, 0xE6B3);
		loop ( i , vp )
		{
			if ( StartCom(vp[i]) )
			{
				StartThread();
				m_isNotifyMode = true;
				BYTE d = 0x30;
				if ( isNotifyMode >= 0 ) 
				{
					d = static_cast<BYTE>(isNotifyMode ? 0x31 : 0x32);	
				}
				TLOG("��Send(0x%02X)", d);
				size_t l = m_rs232c.Send(1, &d);
				if ( l == 1 )
				{
					loop ( i, 5000000 ) 
					{
						if ( IsRecvEndMark() )
						{
							TLOG("       - Recv(0x%02X)", m_recvData);
							if ( m_recvData == 0x31 )
							{
								m_isNotifyMode = true;
								if ( withDialog )
								{
									MessageBox(NULL, "    8255 emulation notify mode    ", "XM6", MB_OK);
								}
								return true;
							}
							else if ( m_recvData == 0x32 )
							{
								m_thread.Stop();
								m_isNotifyMode = false;
								if ( withDialog )
								{
									MessageBox(NULL, "    8255 emulation command mode    ", "XM6", MB_OK);
								}
								return true;
							}
							break;
						}
						::Sleep(0);
					}
				}
				Stop();
			}
		}
		return false;
	}

	/// ��~
	void Stop(void)
	{
		m_rs232c.Close();
		m_thread.Stop();
	}

	/**
	 * �A�N�e�B�u�m�F�H
	 *	@retval true �A�N�e�B�u
	 *	@retval false ��~��
	 */
	bool IsAlive(void) const
	{
		return m_rs232c.IsOpened();
	}

	/**
	 * �R�}���h���s.
	 * @note �ʒm���[�h�ۂ͓����ϐ��l��Ԃ�����
	 *	@param cmd �R�}���h
	 *	@reval -1 ���s�B��A�N�e�B�u�ɂȂ�܂��B
	 *	@reval 0�ȏ� �����B�擾�����l�iBYTE�j���Ԃ�܂��B
	 */
	int ExecCmd(BYTE cmd)
	{
		TLOG("��Send(0x%02X)", cmd);
		if ( m_isNotifyMode )
		{
			switch ( cmd )
			{
			case 0x3A:
				TLOG("     Recv(0x%02X)", m_portA);
				return m_portA;
			case 0x3B:
				TLOG("     Recv(0x%02X)", m_portB);
				return m_portB;
			case 0x3C:
				TLOG("     Recv(0x%02X)", m_portC);
				return m_portC;
			default:
				break;
			}
			return 0;
		}
		m_isRecvEndMark = false;
		size_t r = m_rs232c.Send(1, &cmd);
		if ( r == 1 )
		{
			BYTE b1;
			loop ( i, 50000 ) 
			{
				size_t l = m_rs232c.Receive(1, &b1);
				if ( i == INVALID_SIZE )
				{
					break;
				}
				else if ( l == 1 )
				{
					TLOG("     Recv(0x%02X)", b1);
					return b1;
				}
				::Sleep(0);
			}
		}
		TLOG("       - Timeout");
		Stop();
		MessageBox(NULL, "    end 8255 emulation mode    ", "XM6", MB_OK);
		return -1;
	}

	/**
	 * ���M���s.
	 * @note �ʒm���[�h�ۂ͉������҂��܂��B
	 *	@param cmd �R�}���h
	 *	@param data �f�[�^
	 *	@reval false ���s�B��A�N�e�B�u�ɂȂ�܂��B
	 *	@reval true �����B
	 */
	bool Send(BYTE cmd, BYTE data)
	{
		TLOG("��Send(0x%02X,0x%02X)", cmd, data);
		BYTE b = 0;
		switch ( cmd )
		{
		case 0x4A:
			b = static_cast<BYTE>((data & _BIN(01101111)) | _BIN(10000000));
			break;
		case 0x4B:
			b = static_cast<BYTE>((data & _BIN(01101111)) | _BIN(10010000));
			break;
		case 0x4C:
			b = static_cast<BYTE>(data >> 4);
			break;
		case 0x4D:
			if ( (data & 0x80) != 0 )
			{
				b = static_cast<BYTE>(((data & _BIN(01111000)) >> 1) | (data & _BIN(00000011)) | _BIN(01000000));
			}
			else
			{
				b = static_cast<BYTE>((data & 0x0F) | _BIN(00010000));
			}
			break;
		default:
			break;
		}
		if ( ! m_isNotifyMode )
		{
			size_t l = m_rs232c.Send(1, &b);
			if ( l == 1 )
			{
				return true;
			}
		}
		else 
		{
			size_t l = m_rs232c.Send(1, &b);
			if ( l == 1 )
			{
				loop ( i, 5000000 ) 
				{
					if ( IsRecvEndMark() )
					{
						return true;
					}
					::Sleep(0);
				}
			}
		}
		TLOG("       - Timeout");
		Stop();
		MessageBox(NULL, "    end 8255 emulation mode    ", "XM6", MB_OK);
		return false;
	}

private:
	CThread			m_thread;		///< �X���b�h�N���X
	CRs232c			m_rs232c;		///< RS232C�N���X
	bool			m_isNotifyMode;	///< ���[�h�Btrue�Ȃ�ʒm���[�h, false �Ȃ�R�}���h���[�h
	volatile bool	m_isRecvEndMark;///< �G���h�}�[�N��M�t���O�i�ʒm���[�h�p�j
	BYTE			m_recvData;		///< ��M�f�[�^�i�ʒm���[�h�p�j
	volatile BYTE	m_portA;		///< ��M�f�[�^�i�ʒm���[�h�p�j
	volatile BYTE	m_portB;		///< ��M�f�[�^�i�ʒm���[�h�p�j
	volatile BYTE	m_portC;		///< ��M�f�[�^�i�ʒm���[�h�p�j

	/**
	 * COM�J�n.
	 *	@param port COM�|�[�g�ԍ�
	 *	@retval true ����(����������܂łق�ƂɎg���邩�͂킩��Ȃ�)
	 *	@retval false ���s�B
	 */
	bool StartCom(int port)
	{
		m_rs232c.SetParameter(port, 12000000 * 40, 8, CRs232c::Parity_Non, CRs232c::StopBits_1);
		return m_rs232c.Open();
	}

	/**
	 * �X���b�h�J�n.
	 *	@retval true �����B
	 *	@retval false ���s�B
	 */
	bool StartThread(void)
	{
		m_thread.SetRunner(this);
		return m_thread.Start();
	}

	/**
	 * �I�[��M�����H
	 *	@retval true ��M����
	 *	@retval false �܂��B
	 */
	volatile bool IsRecvEndMark(void)
	{
		bool r = m_isRecvEndMark;
		if (r) 
		{
			m_isRecvEndMark = false;
		}
		return r;
	}

	/**
	 * RS233C�C�x���g�����R�[���o�b�N.
	 *	@return �O
	 */
	virtual DWORD Run(void)
	{
		BYTE b1;
		while ( IsRunnable() )
		{
			size_t l = m_rs232c.Receive(1, &b1);
			if ( l == INVALID_SIZE )
			{
				m_rs232c.Close();
				break;
			}
			else if ( l > 0  )
			{
				if ( (b1 & 0xF0) == 0x30 ) 
				{
					m_recvData = b1;
					m_isRecvEndMark = true;
					TLOG("       - Recv(0x%02X)", b1);
				}
				if ( m_isNotifyMode )
				{
					if ( (b1 & _BIN(10010000)) == _BIN(10000000) )
					{
						// 0x3A
						m_portA = static_cast<BYTE>(b1 | _BIN(10010000));
						TLOG("       - Recv(0x%02X, 0x%02X)", 0x3A, m_portA);
					}
					else if ( (b1 & _BIN(10010000)) == _BIN(10010000) )
					{
						// 0x3B
						m_portB = static_cast<BYTE>(b1 | _BIN(10010000));
						TLOG("       - Recv(0x%02X, 0x%02X)", 0x3B, m_portB);
					}
					else if ( (b1 & _BIN(11110000)) == 0 )
					{
						// 0x3C
						m_portC = static_cast<BYTE>(b1 << 4);
						TLOG("       - Recv(0x%02X, 0x%02X)", 0x3C, m_portC);
					}
				}
			}
			::Sleep(0);
		}
		return 0;
	}
};

