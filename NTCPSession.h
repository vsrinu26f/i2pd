#ifndef NTCP_SESSION_H__
#define NTCP_SESSION_H__

#include <inttypes.h>
#include <map>
#include <memory>
#include <thread>
#include <mutex>
#include <boost/asio.hpp>
#include "Crypto.h"
#include "Identity.h"
#include "RouterInfo.h"
#include "I2NPProtocol.h"
#include "TransportSession.h"

namespace i2p
{
namespace transport
{
	struct NTCPPhase1
	{
		uint8_t pubKey[256];
		uint8_t HXxorHI[32];
	};	
	
	struct NTCPPhase2
	{
		uint8_t pubKey[256];
		struct
		{
			uint8_t hxy[32];
			uint8_t timestamp[4];
			uint8_t filler[12];
		} encrypted;	
	};	

	const size_t NTCP_MAX_MESSAGE_SIZE = 16384; 
	const size_t NTCP_BUFFER_SIZE = 4160; // fits 4 tunnel messages (4*1028)
	const int NTCP_TERMINATION_TIMEOUT = 120; // 2 minutes
	const size_t NTCP_DEFAULT_PHASE3_SIZE = 2/*size*/ + i2p::data::DEFAULT_IDENTITY_SIZE/*387*/ + 4/*ts*/ + 15/*padding*/ + 40/*signature*/; // 448 	
	const int NTCP_BAN_EXPIRATION_TIMEOUT = 70; // in second
	const int NTCP_CLOCK_SKEW = 60; // in seconds 

	class NTCPServer;
	class NTCPSession: public TransportSession, public std::enable_shared_from_this<NTCPSession>
	{
		public:

			NTCPSession (NTCPServer& server, std::shared_ptr<const i2p::data::RouterInfo> in_RemoteRouter = nullptr);
			~NTCPSession ();
			void Terminate ();
			void Done ();

			boost::asio::ip::tcp::socket& GetSocket () { return m_Socket; };
			bool IsEstablished () const { return m_IsEstablished; };
			
			void ClientLogin ();
			void ServerLogin ();
			void SendI2NPMessages (const std::vector<std::shared_ptr<I2NPMessage> >& msgs);
			
		private:

			void PostI2NPMessages (std::vector<std::shared_ptr<I2NPMessage> > msgs);
			void Connected ();
			void SendTimeSyncMessage ();
			void SetIsEstablished (bool isEstablished) { m_IsEstablished = isEstablished; }

			void CreateAESKey (uint8_t * pubKey, i2p::crypto::AESKey& key);
				
			// client
			void SendPhase3 ();
			void HandlePhase1Sent (const boost::system::error_code& ecode,  std::size_t bytes_transferred);
			void HandlePhase2Received (const boost::system::error_code& ecode, std::size_t bytes_transferred);
			void HandlePhase3Sent (const boost::system::error_code& ecode, std::size_t bytes_transferred, uint32_t tsA);
			void HandlePhase4Received (const boost::system::error_code& ecode, std::size_t bytes_transferred, uint32_t tsA);

			//server
			void SendPhase2 ();
			void SendPhase4 (uint32_t tsA, uint32_t tsB);
			void HandlePhase1Received (const boost::system::error_code& ecode, std::size_t bytes_transferred);
			void HandlePhase2Sent (const boost::system::error_code& ecode, std::size_t bytes_transferred, uint32_t tsB);
			void HandlePhase3Received (const boost::system::error_code& ecode, std::size_t bytes_transferred, uint32_t tsB);
			void HandlePhase3ExtraReceived (const boost::system::error_code& ecode, std::size_t bytes_transferred, uint32_t tsB, size_t paddingLen);
			void HandlePhase3 (uint32_t tsB, size_t paddingLen);
			void HandlePhase4Sent (const boost::system::error_code& ecode,  std::size_t bytes_transferred);
			
			// common
			void Receive ();
			void HandleReceived (const boost::system::error_code& ecode, std::size_t bytes_transferred);
			bool DecryptNextBlock (const uint8_t * encrypted);	
		
			void Send (std::shared_ptr<i2p::I2NPMessage> msg);
			boost::asio::const_buffers_1 CreateMsgBuffer (std::shared_ptr<I2NPMessage> msg);
			void Send (const std::vector<std::shared_ptr<I2NPMessage> >& msgs);
			void HandleSent (const boost::system::error_code& ecode, std::size_t bytes_transferred, std::vector<std::shared_ptr<I2NPMessage> > msgs);
			
			
			// timer
			void ScheduleTermination ();
			void HandleTerminationTimer (const boost::system::error_code& ecode);
			
		private:

			NTCPServer& m_Server;
			boost::asio::ip::tcp::socket m_Socket;
			boost::asio::deadline_timer m_TerminationTimer;
			bool m_IsEstablished, m_IsTerminated;
			
			i2p::crypto::CBCDecryption m_Decryption;
			i2p::crypto::CBCEncryption m_Encryption;

			struct Establisher
			{	
				NTCPPhase1 phase1;
				NTCPPhase2 phase2;
			} * m_Establisher;	
			
			i2p::crypto::AESAlignedBuffer<NTCP_BUFFER_SIZE + 16> m_ReceiveBuffer;
			i2p::crypto::AESAlignedBuffer<16> m_TimeSyncBuffer;
			int m_ReceiveBufferOffset; 

			std::shared_ptr<I2NPMessage> m_NextMessage;
			size_t m_NextMessageOffset;
			i2p::I2NPMessagesHandler m_Handler;

			bool m_IsSending;
			std::vector<std::shared_ptr<I2NPMessage> > m_SendQueue;
			
			boost::asio::ip::address m_ConnectedFrom; // for ban
	};	

	// TODO: move to NTCP.h/.cpp
	class NTCPServer
	{
		public:

			NTCPServer ();
			~NTCPServer ();

			void Start ();
			void Stop ();

			bool AddNTCPSession (std::shared_ptr<NTCPSession> session);
			void RemoveNTCPSession (std::shared_ptr<NTCPSession> session);
			std::shared_ptr<NTCPSession> FindNTCPSession (const i2p::data::IdentHash& ident);
			void Connect (const boost::asio::ip::address& address, int port, std::shared_ptr<NTCPSession> conn);

      bool IsBoundV4() const { return m_NTCPAcceptor != nullptr; };
      bool IsBoundV6() const { return m_NTCPV6Acceptor != nullptr; };
      
			boost::asio::io_service& GetService () { return m_Service; };
			void Ban (const boost::asio::ip::address& addr);			

		private:

			void Run ();
			void HandleAccept (std::shared_ptr<NTCPSession> conn, const boost::system::error_code& error);
			void HandleAcceptV6 (std::shared_ptr<NTCPSession> conn, const boost::system::error_code& error);

			void HandleConnect (const boost::system::error_code& ecode, std::shared_ptr<NTCPSession> conn);
			
		private:	

			bool m_IsRunning;
			std::thread * m_Thread;	
			boost::asio::io_service m_Service;
			boost::asio::io_service::work m_Work;
			boost::asio::ip::tcp::acceptor * m_NTCPAcceptor, * m_NTCPV6Acceptor;
			std::map<i2p::data::IdentHash, std::shared_ptr<NTCPSession> > m_NTCPSessions; // access from m_Thread only
			std::map<boost::asio::ip::address, uint32_t> m_BanList; // IP -> ban expiration time in seconds

		public:

			// for HTTP/I2PControl
			const decltype(m_NTCPSessions)& GetNTCPSessions () const { return m_NTCPSessions; };
	};	
}	
}	

#endif
