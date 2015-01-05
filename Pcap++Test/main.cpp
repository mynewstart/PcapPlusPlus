#include <memory>
#include <fstream>
#include <sstream>
#include <debug_new.h>
#include <Logger.h>
#include <IpAddress.h>
#include <MacAddress.h>
#include <Packet.h>
#include <IPv4Layer.h>
#include <TcpLayer.h>
#include <HttpLayer.h>
#include <VlanLayer.h>
#include <UdpLayer.h>
#include <PcapFileDevice.h>
#include <PcapLiveDeviceList.h>
#include <WinPcapLiveDevice.h>
#include <PcapLiveDevice.h>
#include <PcapRemoteDevice.h>
#include <PcapRemoteDeviceList.h>
#include <stdio.h>
#include <string.h>
#include <iostream>
#include <string>
#include <PcapFilter.h>
#include <PlatformSpecificUtils.h>
#include <getopt.h>
#include <stdlib.h>
#ifndef WIN32 //for using ntohl, ntohs, etc.
#include <in.h>
#endif

using namespace std;

#define EXAMPLE_PCAP_WRITE_PATH "PcapExamples/example_copy.pcap"
#define EXAMPLE_PCAP_PATH "PcapExamples/example.pcap"
#define EXAMPLE2_PCAP_PATH "PcapExamples/example2.pcap"
#define EXAMPLE_PCAP_HTTP_REQUEST "PcapExamples/4KHttpRequests.pcap"
#define EXAMPLE_PCAP_HTTP_RESPONSE "PcapExamples/650HttpResponses.pcap"
#define EXAMPLE_PCAP_VLAN "PcapExamples/VlanPackets.pcap"

#define PCAPP_TEST(TestName) bool TestName(PcapTestArgs const& args)

#define PCAPP_ASSERT(exp, assertFailedFormat, ...) \
	if (!(exp)) \
	{ \
		printf("%-30s: FAILED. assertion failed: " assertFailedFormat "\n", __FUNCTION__, ## __VA_ARGS__); \
		return false; \
	}

#define PCAPP_ASSERT_AND_RUN_COMMAND(exp, command, assertFailedFormat, ...) \
	if (!(exp)) \
	{ \
		printf("%-30s: FAILED. assertion failed: " assertFailedFormat "\n", __FUNCTION__, ## __VA_ARGS__); \
		command; \
		return false; \
	}

#define PCAPP_TEST_PASSED printf("%-30s: PASSED\n", __FUNCTION__); return true

#define PCAPP_START_RUNNING_TESTS bool allTestsPassed = true
#define PCAPP_RUN_TEST(TestName, args) allTestsPassed &= TestName(args)
#define PCAPP_END_RUNNING_TESTS \
		if (allTestsPassed) \
			printf("ALL TESTS PASSED!!\n"); \
		else \
			printf("NOT ALL TESTS PASSED!!\n");

struct PcapTestArgs
{
	string ipToSendReceivePackets;
	bool debugMode;
	string remoteIp;
	uint16_t remotePort;
	char* errString;
};

void packetArrives(RawPacket* pRawPacket, PcapLiveDevice* pDevice, void* userCookie)
{
//	EthPacket* pPacket = PacketParser::parsePacket(pRawPacket);
//	if (pPacket->isPacketOfType(IP))
//	{
//		IpPacket* ipPacket = static_cast<IpPacket*>(pPacket);
//		if (ipPacket->getIpVersion() == 4)
//		{
//			iphdr* ip_header = ipPacket->getIPv4Header();
//			uint32_t src_ip = htonl(ip_header->saddr);
//			uint32_t dst_ip = htonl(ip_header->daddr);
//
//			printf("<");
//			PRINT_IPV4_ADDRESS(src_ip);
//			printf("> - ");
//			printf("<");
//			PRINT_IPV4_ADDRESS(dst_ip);
//			printf(">\n");
//		}
//		else if (ipPacket->getIpVersion() == 6)
//		{
//			ip6_hdr* ip_header = ipPacket->getIPv6Header();
//			uint8_t* src_ip = ip_header->src_addr;
//			uint8_t* dst_ip = ip_header->dst_addr;
//
//			printf("<");
//			PRINT_IPV6_ADDRESS(src_ip);
//			printf("> - ");
//			printf("<");
//			PRINT_IPV6_ADDRESS(dst_ip);
//			printf(">\n");
//		}
//	}

	(*(int*)userCookie)++;
}

void statsUpdate(pcap_stat& stats, void* userCookie)
{
	(*(int*)userCookie)++;
}

int getFileLength(const char* filename)
{
	ifstream infile(filename, ifstream::binary);
	if (!infile)
		return -1;
	infile.seekg(0, infile.end);
    int length = infile.tellg();
    infile.close();
    return length;
}

uint8_t* readFileIntoBuffer(const char* filename, int& bufferLength)
{
	int fileLength = getFileLength(filename);
	if (fileLength == -1)
		return NULL;

	ifstream infile(filename);
	if (!infile)
		return NULL;

	bufferLength = fileLength/2 + 2;
	uint8_t* result = new uint8_t[bufferLength];
	int i = 0;
	while (!infile.eof())
	{
		char byte[3];
		infile.read(byte, 2);
		result[i] = (uint8_t)strtol(byte, NULL, 16);
		i++;
	}
	infile.close();
	return result;
}

bool sendURLRequest(string url)
{
	//TODO: what about windows 64?
#ifdef WIN32
	string cmd = "cUrl\\curl_win32.exe -s -o cUrl\\curl_output.txt";
#else
	string cmd = "cUrl/curl.linux32 -s -o cUrl/curl_output.txt";
#endif

	cmd += " " + url;
	if (system(cmd.c_str()) == -1)
		return false;
	return true;
}

#ifdef WIN32
HANDLE activateRpcapdServer(string ip, uint16_t port)
{
	char portAsString[10];
	sprintf(portAsString, "%d", port);
	string cmd = "rpcapd\\rpcapd.exe";
	string args = "rpcapd\\rpcapd.exe -b " + ip + " -p " + string(portAsString) + " -n";

	STARTUPINFO si;
	PROCESS_INFORMATION pi;

	ZeroMemory( &si, sizeof(si) );
	si.cb = sizeof(si);
	ZeroMemory( &pi, sizeof(pi) );
	if (!CreateProcess
			(
			TEXT(cmd.c_str()),
			(char*)TEXT(args.c_str()),
			NULL,NULL,FALSE,
			CREATE_NEW_CONSOLE,
			NULL,NULL,
			&si,
			&pi
			)
			)
		{
			return NULL;
		}

	return pi.hProcess;
}

void terminateRpcapdServer(HANDLE processHandle)
{
	if (processHandle != NULL)
		TerminateProcess(processHandle, 0);
}

#endif

PCAPP_TEST(TestIPAddress)
{
	auto_ptr<IPAddress> ip4Addr = IPAddress::fromString((char*)"10.0.0.4");
	PCAPP_ASSERT(ip4Addr.get() != NULL, "IPv4 address is NULL");
	PCAPP_ASSERT(ip4Addr->getType() == IPAddress::IPv4AddressType, "IPv4 address is not of type IPv4Address");
	PCAPP_ASSERT(strcmp(ip4Addr->toString().c_str(), "10.0.0.4") == 0, "IPv4 toString doesn't return the correct string");
	IPv4Address* ip4AddrAfterCast = static_cast<IPv4Address*>(ip4Addr.get());
	PCAPP_ASSERT(ntohl(ip4AddrAfterCast->toInt()) == 0x0A000004, "toInt() gave wrong result: %X", ip4AddrAfterCast->toInt());

	string ip6AddrString("2607:f0d0:1002:51::4");
	auto_ptr<IPAddress> ip6Addr = IPAddress::fromString(ip6AddrString);
	PCAPP_ASSERT(ip6Addr.get() != NULL, "IPv6 address is NULL");
	PCAPP_ASSERT(ip6Addr->getType() == IPAddress::IPv6AddressType, "IPv6 address is not of type IPv6Address");
	PCAPP_ASSERT(strcmp(ip6Addr->toString().c_str(), "2607:f0d0:1002:51::4") == 0, "IPv6 toString doesn't return the correct string");
	IPv6Address* ip6AddrAfterCast = static_cast<IPv6Address*>(ip6Addr.get());
	size_t length = 0;
	uint8_t* addrAsByteArray;
	ip6AddrAfterCast->copyTo(&addrAsByteArray, length);
	PCAPP_ASSERT(length == 16, "IPv6 packet length is wrong. Expected 16, got %d", length);
	uint8_t expectedByteArray[16] = { 0x26, 0x07, 0xF0, 0xD0, 0x10, 0x02, 0x00, 0x51, 0x00, 0x00 , 0x00, 0x00, 0x00, 0x00, 0x00, 0x04 };
	for (int i = 0; i < 16; i++)
		PCAPP_ASSERT(addrAsByteArray[i] == expectedByteArray[i], "Failed to convert IPv6 address to byte array; byte #%d: expected 0x%X got 0x%X", i, expectedByteArray[i], addrAsByteArray[i]);

	delete [] addrAsByteArray;
	ip6Addr = IPAddress::fromString(string("2607:f0d0:1002:0051:0000:0000:0000:0004"));
	PCAPP_ASSERT(ip6Addr.get() != NULL, "IPv6 address is NULL");
	PCAPP_ASSERT(ip6Addr->getType() == IPAddress::IPv6AddressType, "IPv6 address is not of type IPv6Address");
	PCAPP_ASSERT(strcmp(ip6Addr->toString().c_str(), "2607:f0d0:1002:0051:0000:0000:0000:0004") == 0, "IPv6 toString doesn't return the correct string");

	PCAPP_TEST_PASSED;
}

PCAPP_TEST(TestMacAddress)
{
	MacAddress macAddr1(0x11,0x2,0x33,0x4,0x55,0x6);
	PCAPP_ASSERT(macAddr1.isValid(), "macAddr1 is not valid");
	MacAddress macAddr2(0x11,0x2,0x33,0x4,0x55,0x6);
	PCAPP_ASSERT(macAddr2.isValid(), "macAddr2 is not valid");
	PCAPP_ASSERT(macAddr1 == macAddr2, "Equal operator failed");

	MacAddress macAddr3(string("11:02:33:04:55:06"));
	PCAPP_ASSERT(macAddr3.isValid(), "macAddr3 is not valid");
	PCAPP_ASSERT(macAddr1 == macAddr3, "Different c'tors with same MAC address (string and by octets) give different addresses");

	uint8_t addrAsArr[6] = { 0x11, 0x2, 0x33, 0x4, 0x55, 0x6 };
	MacAddress macAddr4(addrAsArr);
	PCAPP_ASSERT(macAddr4.isValid(), "macAddr4 is not valid");
	PCAPP_ASSERT(macAddr1 == macAddr4, "Different c'tors with same MAC address (from arr and by octets) give different addresses");

	string macAsStr = macAddr1.toString();
	PCAPP_ASSERT(macAsStr == string("11:02:33:04:55:06"), "String representation failure: expected '%s', got '%s'", "11:02:33:04:55:06", macAddr1.toString().c_str());

	uint8_t* arrToCopyTo = NULL;
	macAddr3.copyTo(&arrToCopyTo);
	PCAPP_ASSERT(arrToCopyTo[0] == 0x11 && arrToCopyTo[1] == 0x02 && arrToCopyTo[2] == 0x33 && arrToCopyTo[3] == 0x04 && arrToCopyTo[4] == 0x55 && arrToCopyTo[5] == 0x06, "Copy MacAddress to array failed");

	delete [] arrToCopyTo;

	PCAPP_TEST_PASSED;
}


PCAPP_TEST(TestPcapFileReadWrite)
{
    PcapFileReaderDevice readerDev(EXAMPLE_PCAP_PATH);
    PcapFileWriterDevice writerDev(EXAMPLE_PCAP_WRITE_PATH);
    PCAPP_ASSERT(readerDev.open(), "cannot open reader device");
    PCAPP_ASSERT(writerDev.open(), "cannot open writer device");
    RawPacket rawPacket;
    int packetCount = 0;
    int ethCount = 0;
    int ipCount = 0;
    int tcpCount = 0;
    int udpCount = 0;
    while (readerDev.getNextPacket(rawPacket))
    {
    	packetCount++;
    	Packet packet(&rawPacket);
		if (packet.isPacketOfType(Ethernet))
			ethCount++;
		if (packet.isPacketOfType(IPv4))
			ipCount++;
		if (packet.isPacketOfType(TCP))
			tcpCount++;
		if (packet.isPacketOfType(UDP))
			udpCount++;

		writerDev.writePacket(rawPacket);
    }


    pcap_stat readerStatistics;
    pcap_stat writerStatistics;

    readerDev.getStatistics(readerStatistics);
    PCAPP_ASSERT(readerStatistics.ps_recv == 4631, "Incorrect number of packets read from file. Expected: 4631; read: %d", readerStatistics.ps_recv);
    PCAPP_ASSERT(readerStatistics.ps_drop == 0, "Packets were not read properly from file. Number of packets dropped: %d", readerStatistics.ps_drop);

    writerDev.getStatistics(writerStatistics);
    PCAPP_ASSERT(writerStatistics.ps_recv == 4631, "Incorrect number of packets written to file. Expected: 4631; read: %d", writerStatistics.ps_recv);
    PCAPP_ASSERT(writerStatistics.ps_drop == 0, "Packets were not written properly to file. Number of packets dropped: %d", writerStatistics.ps_drop);

    PCAPP_ASSERT(ethCount == 4631, "Incorrect number of Ethernet packets read. Expected: 4631; read: %d", ethCount);
    PCAPP_ASSERT(ipCount == 4631, "Incorrect number of IPv4 packets read. Expected: 4631; read: %d", ipCount);
    PCAPP_ASSERT(tcpCount == 4492, "Incorrect number of IPv4 packets read. Expected: 4492; read: %d", tcpCount);
    PCAPP_ASSERT(udpCount == 139, "Incorrect number of IPv4 packets read. Expected: 139; read: %d", udpCount);

    readerDev.close();
    writerDev.close();

    PCAPP_TEST_PASSED;
}

PCAPP_TEST(TestPcapLiveDeviceList)
{
    vector<PcapLiveDevice*> devList = PcapLiveDeviceList::getInstance().getPcapLiveDevicesList();
    PCAPP_ASSERT(!devList.empty(), "Device list is empty");

    for(vector<PcapLiveDevice*>::iterator iter = devList.begin(); iter != devList.end(); iter++)
    {
    	PCAPP_ASSERT(!((*iter)->getName() == NULL), "Device name is NULL");
    }

    PCAPP_TEST_PASSED;
}

PCAPP_TEST(TestPcapLiveDeviceListSearch)
{
	PcapLiveDevice* liveDev = NULL;
    liveDev = PcapLiveDeviceList::getInstance().getPcapLiveDeviceByIp(args.ipToSendReceivePackets.c_str());
    PCAPP_ASSERT(liveDev != NULL, "Device used in this test %s doesn't exist", args.ipToSendReceivePackets.c_str());

    liveDev = PcapLiveDeviceList::getInstance().getPcapLiveDeviceByIp("255.255.255.250");
    PCAPP_ASSERT(liveDev == NULL, "Illegal device found with IP=255.255.255.250");

    PCAPP_TEST_PASSED;
}

PCAPP_TEST(TestPcapLiveDevice)
{
	PcapLiveDevice* liveDev = NULL;
    IPv4Address ipToSearch(args.ipToSendReceivePackets.c_str());
    liveDev = PcapLiveDeviceList::getInstance().getPcapLiveDeviceByIp(ipToSearch);
    PCAPP_ASSERT(liveDev != NULL, "Device used in this test %s doesn't exist", args.ipToSendReceivePackets.c_str());
    PCAPP_ASSERT(liveDev->getMtu() > 0, "Could not get live device MTU");
    PCAPP_ASSERT(liveDev->open(), "Cannot open live device");
    int packetCount = 0;
    int numOfTimeStatsWereInvoked = 0;
    PCAPP_ASSERT(liveDev->startCapture(&packetArrives, (void*)&packetCount, 1, &statsUpdate, (void*)&numOfTimeStatsWereInvoked), "Cannot start capture");
    PCAP_SLEEP(20);
    liveDev->stopCapture();
    PCAPP_ASSERT(packetCount > 0, "No packets were captured");
    PCAPP_ASSERT(numOfTimeStatsWereInvoked > 18, "Stat callback was called less than expected: %d", numOfTimeStatsWereInvoked);
    pcap_stat statistics;
    liveDev->getStatistics(statistics);
    PCAPP_ASSERT(statistics.ps_drop == 0, "Packets were dropped: %d", statistics.ps_drop);
    liveDev->close();

    PCAPP_TEST_PASSED;
}

PCAPP_TEST(TestPcapLiveDeviceStatsMode)
{
	PcapLiveDevice* liveDev = PcapLiveDeviceList::getInstance().getPcapLiveDeviceByIp(args.ipToSendReceivePackets.c_str());
	PCAPP_ASSERT(liveDev != NULL, "Device used in this test %s doesn't exist", args.ipToSendReceivePackets.c_str());
	PCAPP_ASSERT(liveDev->open(), "Cannot open live device");
	int numOfTimeStatsWereInvoked = 0;
	PCAPP_ASSERT(liveDev->startCapture(1, &statsUpdate, (void*)&numOfTimeStatsWereInvoked), "Cannot start capture");
	sendURLRequest("www.ebay.com");
	PCAP_SLEEP(5);
	liveDev->stopCapture();
	PCAPP_ASSERT(numOfTimeStatsWereInvoked >= 4, "Stat callback was called less than expected: %d", numOfTimeStatsWereInvoked);
    pcap_stat statistics;
    liveDev->getStatistics(statistics);
    PCAPP_ASSERT(statistics.ps_recv > 2, "No packets were captured");
    PCAPP_ASSERT(statistics.ps_drop == 0, "Packets were dropped: %d", statistics.ps_drop);
    liveDev->close();
    PCAPP_TEST_PASSED;
}

PCAPP_TEST(TestWinPcapLiveDevice)
{
#ifdef WIN32
	PcapLiveDevice* liveDev = PcapLiveDeviceList::getInstance().getPcapLiveDeviceByIp(args.ipToSendReceivePackets.c_str());
	PCAPP_ASSERT(liveDev != NULL, "Device used in this test %s doesn't exist", args.ipToSendReceivePackets.c_str());
	PCAPP_ASSERT(liveDev->getDeviceType() == PcapLiveDevice::WinPcapDevice, "Live device is not of type LibPcapDevice");

	WinPcapLiveDevice* pWinPcapLiveDevice = static_cast<WinPcapLiveDevice*>(liveDev);
	int defaultDataToCopy = pWinPcapLiveDevice->getMinAmountOfDataToCopyFromKernelToApplication();
	PCAPP_ASSERT(defaultDataToCopy == 16000, "Data to copy isn't at its default size (16000)");
	PCAPP_ASSERT(pWinPcapLiveDevice->open(), "Cannot open live device");
	PCAPP_ASSERT(pWinPcapLiveDevice->setMinAmountOfDataToCopyFromKernelToApplication(100000), "Set data to copy to 100000 failed. Error string: %s", args.errString);
    int packetCount = 0;
    int numOfTimeStatsWereInvoked = 0;
    PCAPP_ASSERT(pWinPcapLiveDevice->startCapture(&packetArrives, (void*)&packetCount, 1, &statsUpdate, (void*)&numOfTimeStatsWereInvoked), "Cannot start capture");
	for (int i = 0; i < 5; i++)
		sendURLRequest("www.ebay.com");
	pcap_stat statistics;
	pWinPcapLiveDevice->getStatistics(statistics);
    PCAPP_ASSERT(statistics.ps_recv > 20, "No packets were captured");
    PCAPP_ASSERT(statistics.ps_drop == 0, "Packets were dropped: %d", statistics.ps_drop);
    pWinPcapLiveDevice->stopCapture();
	PCAPP_ASSERT(pWinPcapLiveDevice->setMinAmountOfDataToCopyFromKernelToApplication(defaultDataToCopy), "Could not set data to copy back to default value. Error string: %s", args.errString);
	pWinPcapLiveDevice->close();
#else
	PcapLiveDevice* liveDev = PcapLiveDeviceList::getInstance().getPcapLiveDeviceByIp(args.ipToSendReceivePackets.c_str());
	PCAPP_ASSERT(liveDev->getDeviceType() == PcapLiveDevice::LibPcapDevice, "Live device is not of type LibPcapDevice");
#endif

	PCAPP_TEST_PASSED;
}

PCAPP_TEST(TestPcapFilters)
{
	PcapLiveDevice* liveDev = NULL;
    IPv4Address ipToSearch(args.ipToSendReceivePackets.c_str());
    liveDev = PcapLiveDeviceList::getInstance().getPcapLiveDeviceByIp(ipToSearch);
    PCAPP_ASSERT(liveDev != NULL, "Device used in this test %s doesn't exist", args.ipToSendReceivePackets.c_str());

    string filterAsString;
    PCAPP_ASSERT(liveDev->open(), "Cannot open live device");
    RawPacketVector capturedPackets;

    //-----------
    //IP filter
    //-----------
    string filterAddrAsString(args.ipToSendReceivePackets);
    IPFilter ipFilter(filterAddrAsString, DST);
    ipFilter.parseToString(filterAsString);
    PCAPP_ASSERT(liveDev->setFilter(ipFilter), "Could not set filter: %s", filterAsString.c_str());
    PCAPP_ASSERT(liveDev->startCapture(capturedPackets), "Cannot start capture for filter '%s'", filterAsString.c_str());
    PCAPP_ASSERT(sendURLRequest("www.google.com"), "Could not send URL request for filter '%s'", filterAsString.c_str());
    //let the capture work for couple of seconds
	PCAP_SLEEP(2);
	liveDev->stopCapture();
	PCAPP_ASSERT(capturedPackets.size() >= 2, "Captured less than 2 packets (HTTP request and response)");


	for (RawPacketVector::VectorIterator iter = capturedPackets.begin(); iter != capturedPackets.end(); iter++)
	{
		Packet packet(*iter);
		PCAPP_ASSERT(packet.isPacketOfType(IPv4), "Filter '%s', Packet captured isn't of type IP", filterAsString.c_str());
		IPv4Layer* ipv4Layer = packet.getLayerOfType<IPv4Layer>();
		PCAPP_ASSERT(ipv4Layer->getIPv4Header()->ipDst == ipToSearch.toInt(), "'IP Filter' failed. Packet IP dst is %X, expected %X", ipv4Layer->getIPv4Header()->ipDst, ipToSearch.toInt());
	}

    //------------
    //Port filter
    //------------
    uint16_t filterPort = 80;
    PortFilter portFilter(filterPort, SRC);
    portFilter.parseToString(filterAsString);
    PCAPP_ASSERT(liveDev->setFilter(portFilter), "Could not set filter: %s", filterAsString.c_str());
    PCAPP_ASSERT(liveDev->startCapture(capturedPackets), "Cannot start capture for filter '%s'", filterAsString.c_str());
    PCAPP_ASSERT(sendURLRequest("www.yahoo.com"), "Could not send URL request for filter '%s'", filterAsString.c_str());
    //let the capture work for couple of seconds
	PCAP_SLEEP(2);
	liveDev->stopCapture();
	PCAPP_ASSERT(capturedPackets.size() >= 2, "Captured less than 2 packets (HTTP request and response)");
	for (RawPacketVector::VectorIterator iter = capturedPackets.begin(); iter != capturedPackets.end(); iter++)
	{
		Packet packet(*iter);
		PCAPP_ASSERT(packet.isPacketOfType(TCP), "Filter '%s', Packet captured isn't of type TCP", filterAsString.c_str());
		TcpLayer* pTcpLayer = packet.getLayerOfType<TcpLayer>();
		PCAPP_ASSERT(ntohs(pTcpLayer->getTcpHeader()->portSrc) == 80, "'Port Filter' failed. Packet port src is %d, expected 80", pTcpLayer->getTcpHeader()->portSrc);
	}
	capturedPackets.clear();

    //----------------
    //IP & Port filter
    //----------------
    std::vector<GeneralFilter*> andFilterFilters;
    andFilterFilters.push_back(&ipFilter);
    andFilterFilters.push_back(&portFilter);
    AndFilter andFilter(andFilterFilters);
    andFilter.parseToString(filterAsString);
    PCAPP_ASSERT(liveDev->setFilter(andFilter), "Could not set filter: %s", filterAsString.c_str());
    PCAPP_ASSERT(liveDev->startCapture(capturedPackets), "Cannot start capture for filter '%s'", filterAsString.c_str());
    PCAPP_ASSERT(sendURLRequest("www.walla.co.il"), "Could not send URL request for filter '%s'", filterAsString.c_str());
    //let the capture work for couple of seconds
	PCAP_SLEEP(2);
	liveDev->stopCapture();
	PCAPP_ASSERT(capturedPackets.size() >= 2, "Captured less than 2 packets (HTTP request and response)");
	for (RawPacketVector::VectorIterator iter = capturedPackets.begin(); iter != capturedPackets.end(); iter++)
	{
		Packet packet(*iter);
		PCAPP_ASSERT(packet.isPacketOfType(TCP), "Filter '%s', Packet captured isn't of type TCP", filterAsString.c_str());
		TcpLayer* pTcpLayer = packet.getLayerOfType<TcpLayer>();
		IPv4Layer* pIPv4Layer = packet.getLayerOfType<IPv4Layer>();
		PCAPP_ASSERT(ntohs(pTcpLayer->getTcpHeader()->portSrc) == 80, "'And Filter' failed. Packet port src is %d, expected 80", pTcpLayer->getTcpHeader()->portSrc);
		PCAPP_ASSERT(pIPv4Layer->getIPv4Header()->ipDst == ipToSearch.toInt(), "Filter failed. Packet IP dst is %X, expected %X", pIPv4Layer->getIPv4Header()->ipDst, ipToSearch.toInt());
	}
	capturedPackets.clear();

    //-----------------
    //IP || Port filter
    //-----------------
    std::vector<GeneralFilter*> orFilterFilters;
    ipFilter.setDirection(SRC);
    orFilterFilters.push_back(&ipFilter);
    orFilterFilters.push_back(&portFilter);
    OrFilter orFilter(orFilterFilters);
    orFilter.parseToString(filterAsString);
    PCAPP_ASSERT(liveDev->setFilter(orFilter), "Could not set filter: %s", filterAsString.c_str());
    PCAPP_ASSERT(liveDev->startCapture(capturedPackets), "Cannot start capture for filter '%s'", filterAsString.c_str());
    PCAPP_ASSERT(sendURLRequest("www.youtube.com"), "Could not send URL request for filter '%s'", filterAsString.c_str());
    //let the capture work for couple of seconds
	PCAP_SLEEP(2);
	liveDev->stopCapture();
	PCAPP_ASSERT(capturedPackets.size() >= 2, "Captured less than 2 packets (HTTP request and response)");
	for (RawPacketVector::VectorIterator iter = capturedPackets.begin(); iter != capturedPackets.end(); iter++)
	{
		Packet packet(*iter);
		if (packet.isPacketOfType(TCP))
		{
			TcpLayer* pTcpLayer = packet.getLayerOfType<TcpLayer>();
			bool srcPortMatch = ntohs(pTcpLayer->getTcpHeader()->portSrc) == 80;
			IPv4Layer* pIPv4Layer = packet.getLayerOfType<IPv4Layer>();
			bool srcIpMatch = pIPv4Layer->getIPv4Header()->ipSrc == ipToSearch.toInt();
			PCAPP_ASSERT(srcIpMatch || srcPortMatch, "'Or Filter' failed. Src port is: %d; Src IP is: %X, Expected: port 80 or IP %s", ntohs(pTcpLayer->getTcpHeader()->portSrc), pIPv4Layer->getIPv4Header()->ipSrc, args.ipToSendReceivePackets.c_str());
		} else
		if (packet.isPacketOfType(IP))
		{
			IPv4Layer* pIPv4Layer = packet.getLayerOfType<IPv4Layer>();
			PCAPP_ASSERT(pIPv4Layer->getIPv4Header()->ipSrc == ipToSearch.toInt(), "Filter failed. Packet IP src is %X, expected %X", pIPv4Layer->getIPv4Header()->ipSrc, ipToSearch.toInt());
		}
		else
			PCAPP_ASSERT(true, "Filter '%s', Packet isn't of type IP or TCP", filterAddrAsString.c_str());
	}
	capturedPackets.clear();

    //----------
    //Not filter
    //----------
    NotFilter notFilter(&ipFilter);
    notFilter.parseToString(filterAsString);
    PCAPP_ASSERT(liveDev->setFilter(notFilter), "Could not set filter: %s", filterAsString.c_str());
    PCAPP_ASSERT(liveDev->startCapture(capturedPackets), "Cannot start capture for filter '%s'", filterAsString.c_str());
    PCAPP_ASSERT(sendURLRequest("www.ebay.com"), "Could not send URL request for filter '%s'", filterAsString.c_str());
    //let the capture work for couple of seconds
	PCAP_SLEEP(2);
	liveDev->stopCapture();
	PCAPP_ASSERT(capturedPackets.size() >= 2, "Captured less than 2 packets (HTTP request and response)");
	for (RawPacketVector::VectorIterator iter = capturedPackets.begin(); iter != capturedPackets.end(); iter++)
	{
		Packet packet(*iter);
		if (packet.isPacketOfType(IPv4))
		{
			IPv4Layer* ipv4Layer = packet.getLayerOfType<IPv4Layer>();
			PCAPP_ASSERT(ipv4Layer->getIPv4Header()->ipSrc != ipToSearch.toInt(), "'Not Filter' failed. Packet IP src is %X, the same as %X", ipv4Layer->getIPv4Header()->ipSrc, ipToSearch.toInt());
		}
	}
	capturedPackets.clear();

    //-----------------
    //VLAN filter
    //-----------------
	VlanFilter vlanFilter(118);
	vlanFilter.parseToString(filterAsString);
    PCAPP_ASSERT(liveDev->setFilter(vlanFilter), "Could not set filter: %s", filterAsString.c_str());
    PCAPP_ASSERT(liveDev->startCapture(capturedPackets), "Cannot start capture for filter '%s'", filterAsString.c_str());
    PcapFileReaderDevice fileReaderDev(EXAMPLE_PCAP_VLAN);
    PCAPP_ASSERT(fileReaderDev.open(), "Cannot open file reader device for filter '%s'", filterAsString.c_str());
    RawPacket rawPacket;
    while (fileReaderDev.getNextPacket(rawPacket))
    {
    	PCAPP_ASSERT(liveDev->sendPacket(rawPacket), "Could not send packet. Testing filter: '%s'", filterAsString.c_str());
    }
    fileReaderDev.close();
    PCAP_SLEEP(2);
    liveDev->stopCapture();

    PCAPP_ASSERT(capturedPackets.size() >= 12, "VLAN filter test: Captured: %d packets. Expected: > %d packets", capturedPackets.size(), 12);
    for (RawPacketVector::VectorIterator iter = capturedPackets.begin(); iter != capturedPackets.end(); iter++)
    {
    	Packet packet(*iter);
    	PCAPP_ASSERT(packet.isPacketOfType(VLAN), "VLAN filter test: one of the captured packets isn't of type VLAN");
    	VlanLayer* vlanLayer = packet.getLayerOfType<VlanLayer>();
    	PCAPP_ASSERT(vlanLayer->getVlanID() == 118, "VLAN filter test: VLAN ID != 118, it's: %d", vlanLayer->getVlanID());
    }

    capturedPackets.clear();

    //--------------------
    //MacAddress filter
    //--------------------
    MacAddress macAddrToFilter("00:13:c3:df:ae:18");
    MacAddressFilter macAddrFilter(macAddrToFilter, DST);
    macAddrFilter.parseToString(filterAsString);
    PCAPP_ASSERT(liveDev->setFilter(macAddrFilter), "Could not set filter: %s", filterAsString.c_str());
    PCAPP_ASSERT(liveDev->startCapture(capturedPackets), "Cannot start capture for filter '%s'", filterAsString.c_str());
    PCAPP_ASSERT(fileReaderDev.open(), "Cannot open file reader device for filter '%s'", filterAsString.c_str());
    while (fileReaderDev.getNextPacket(rawPacket))
    {
    	PCAPP_ASSERT(liveDev->sendPacket(rawPacket), "Could not send packet. Testing filter: '%s'", filterAsString.c_str());
    }
    fileReaderDev.close();
    PCAP_SLEEP(2);
    liveDev->stopCapture();

    PCAPP_ASSERT(capturedPackets.size() == 5, "MacAddress test: Captured: %d packets. Expected: %d packets", capturedPackets.size(), 5);
    for (RawPacketVector::VectorIterator iter = capturedPackets.begin(); iter != capturedPackets.end(); iter++)
    {
    	Packet packet(*iter);
    	EthLayer* ethLayer = packet.getLayerOfType<EthLayer>();
    	PCAPP_ASSERT(ethLayer->getDestMac() == macAddrToFilter, "MacAddress test: dest MAC different than expected, it's: '%s'", ethLayer->getDestMac().toString().c_str());
    }

    capturedPackets.clear();

    //--------------------
    //EtherType filter
    //--------------------
    EtherTypeFilter ethTypeFiler(ETHERTYPE_VLAN);
    ethTypeFiler.parseToString(filterAsString);
    PCAPP_ASSERT(liveDev->setFilter(ethTypeFiler), "Could not set filter: %s", filterAsString.c_str());
    PCAPP_ASSERT(liveDev->startCapture(capturedPackets), "Cannot start capture for filter '%s'", filterAsString.c_str());
    PCAPP_ASSERT(fileReaderDev.open(), "Cannot open file reader device for filter '%s'", filterAsString.c_str());
    while (fileReaderDev.getNextPacket(rawPacket))
    {
    	PCAPP_ASSERT(liveDev->sendPacket(rawPacket), "Could not send packet. Testing filter: '%s'", filterAsString.c_str());
    }
    fileReaderDev.close();
    liveDev->stopCapture();
    PCAPP_ASSERT(capturedPackets.size() >= 24, "EthTypeFilter test: Captured less than %d packets", 24);
    for (RawPacketVector::VectorIterator iter = capturedPackets.begin(); iter != capturedPackets.end(); iter++)
    {
    	Packet packet(*iter);
    	PCAPP_ASSERT(packet.isPacketOfType(VLAN), "EthTypeFilter test: one of the captured packets isn't of type VLAN");
    }

    capturedPackets.clear();


    //--------------------
    //IpV4 ID filter
    //--------------------
    uint16_t ipID(0x9900);
    IpV4IDFilter ipIDFiler(ipID, GREATER_THAN);
    ipIDFiler.parseToString(filterAsString);
    PCAPP_ASSERT(liveDev->setFilter(ipIDFiler), "Could not set filter: %s", filterAsString.c_str());
    PCAPP_ASSERT(liveDev->startCapture(capturedPackets), "Cannot start capture for filter '%s'", filterAsString.c_str());
    PcapFileReaderDevice fileReaderDev2(EXAMPLE_PCAP_PATH);
    PCAPP_ASSERT(fileReaderDev2.open(), "Cannot open file reader device for filter '%s'", filterAsString.c_str());
    while (fileReaderDev2.getNextPacket(rawPacket))
    {
    	PCAPP_ASSERT(liveDev->sendPacket(rawPacket), "Could not send packet. Testing filter: '%s'", filterAsString.c_str());
    }
    fileReaderDev2.close();
    liveDev->stopCapture();
    PCAPP_ASSERT(capturedPackets.size() >= 1423, "IpV4IDFilter test: Captured less than %d packets", 1423);
    for (RawPacketVector::VectorIterator iter = capturedPackets.begin(); iter != capturedPackets.end(); iter++)
    {
    	Packet packet(*iter);
    	PCAPP_ASSERT(packet.isPacketOfType(IPv4), "IpV4IDFilter test: one of the captured packets isn't of type IPv4");
		IPv4Layer* ipv4Layer = packet.getLayerOfType<IPv4Layer>();
		PCAPP_ASSERT(ntohs(ipv4Layer->getIPv4Header()->ipId) > ipID, "IpV4IDFilter test: IP ID less than %d, it's %d", ipID, ntohs(ipv4Layer->getIPv4Header()->ipId));
    }

    capturedPackets.clear();

    //-------------------------
    //IpV4 Total Length filter
    //-------------------------
    uint16_t totalLength(576);
    IpV4TotalLengthFilter ipTotalLengthFiler(totalLength, LESS_OR_EQUAL);
    ipTotalLengthFiler.parseToString(filterAsString);
    PCAPP_ASSERT(liveDev->setFilter(ipTotalLengthFiler), "Could not set filter: %s", filterAsString.c_str());
    PCAPP_ASSERT(liveDev->startCapture(capturedPackets), "Cannot start capture for filter '%s'", filterAsString.c_str());
    PCAPP_ASSERT(fileReaderDev2.open(), "Cannot open file reader device for filter '%s'", filterAsString.c_str());
    while (fileReaderDev2.getNextPacket(rawPacket))
    {
    	PCAPP_ASSERT(liveDev->sendPacket(rawPacket), "Could not send packet. Testing filter: '%s'", filterAsString.c_str());
    }
    fileReaderDev2.close();
    liveDev->stopCapture();
    PCAPP_ASSERT(capturedPackets.size() >= 2066, "IpV4TotalLengthFilter test: Captured less than %d packets", 2066);
    for (RawPacketVector::VectorIterator iter = capturedPackets.begin(); iter != capturedPackets.end(); iter++)
    {
    	Packet packet(*iter);
    	PCAPP_ASSERT(packet.isPacketOfType(IPv4), "IpV4TotalLengthFilter test: one of the captured packets isn't of type IPv4");
		IPv4Layer* ipv4Layer = packet.getLayerOfType<IPv4Layer>();
		PCAPP_ASSERT(ntohs(ipv4Layer->getIPv4Header()->totalLength) <= totalLength, "IpV4TotalLengthFilter test: IP total length more than %d, it's %d", totalLength, ntohs(ipv4Layer->getIPv4Header()->totalLength));
    }

    capturedPackets.clear();


    //-------------------------
    //TCP window size filter
    //-------------------------
    uint16_t windowSize(8312);
    TcpWindowSizeFilter tcpWindowSizeFilter(windowSize, NOT_EQUALS);
    tcpWindowSizeFilter.parseToString(filterAsString);
    PCAPP_ASSERT(liveDev->setFilter(tcpWindowSizeFilter), "Could not set filter: %s", filterAsString.c_str());
    PCAPP_ASSERT(liveDev->startCapture(capturedPackets), "Cannot start capture for filter '%s'", filterAsString.c_str());
    PCAPP_ASSERT(fileReaderDev2.open(), "Cannot open file reader device for filter '%s'", filterAsString.c_str());
    while (fileReaderDev2.getNextPacket(rawPacket))
    {
    	PCAPP_ASSERT(liveDev->sendPacket(rawPacket), "Could not send packet. Testing filter: '%s'", filterAsString.c_str());
    }
    fileReaderDev2.close();
    liveDev->stopCapture();
    PCAPP_ASSERT(capturedPackets.size() >= 4249, "TcpWindowSizeFilter test: Captured less than %d packets", 4249);
    for (RawPacketVector::VectorIterator iter = capturedPackets.begin(); iter != capturedPackets.end(); iter++)
    {
    	Packet packet(*iter);
    	PCAPP_ASSERT(packet.isPacketOfType(TCP), "TcpWindowSizeFilter test: one of the captured packets isn't of type TCP");
		TcpLayer* tcpLayer = packet.getLayerOfType<TcpLayer>();
		PCAPP_ASSERT(ntohs(tcpLayer->getTcpHeader()->windowSize) != windowSize, "TcpWindowSizeFilter test: TCP window size equals %d", windowSize);
    }

    capturedPackets.clear();


    //-------------------------
    //UDP length filter
    //-------------------------
    uint16_t udpLength(46);
    UdpLengthFilter udpLengthFilter(udpLength, EQUALS);
    udpLengthFilter.parseToString(filterAsString);
    PCAPP_ASSERT(liveDev->setFilter(udpLengthFilter), "Could not set filter: %s", filterAsString.c_str());
    PCAPP_ASSERT(liveDev->startCapture(capturedPackets), "Cannot start capture for filter '%s'", filterAsString.c_str());
    PCAPP_ASSERT(fileReaderDev2.open(), "Cannot open file reader device for filter '%s'", filterAsString.c_str());
    while (fileReaderDev2.getNextPacket(rawPacket))
    {
    	PCAPP_ASSERT(liveDev->sendPacket(rawPacket), "Could not send packet. Testing filter: '%s'", filterAsString.c_str());
    }
    fileReaderDev2.close();
    liveDev->stopCapture();
    PCAPP_ASSERT(capturedPackets.size() >= 4, "UdpLengthFilter test: Captured less than %d packets", 4);
    for (RawPacketVector::VectorIterator iter = capturedPackets.begin(); iter != capturedPackets.end(); iter++)
    {
    	Packet packet(*iter);
    	PCAPP_ASSERT(packet.isPacketOfType(UDP), "UdpLengthFilter test: one of the captured packets isn't of type UDP");
    	UdpLayer* udpLayer = packet.getLayerOfType<UdpLayer>();
		PCAPP_ASSERT(ntohs(udpLayer->getUdpHeader()->length) == udpLength, "UdpLengthFilter test: UDP length != %d, it's %d", udpLength, ntohs(udpLayer->getUdpHeader()->length));
    }

    capturedPackets.clear();


    //-------------------------
    //TCP flags filter
    //-------------------------
    uint8_t tcpFlagsBitMask(TcpFlagsFilter::tcpSyn|TcpFlagsFilter::tcpAck);
    TcpFlagsFilter tcpFlagsFilter(tcpFlagsBitMask, TcpFlagsFilter::MatchAll);
    tcpFlagsFilter.parseToString(filterAsString);
    PCAPP_ASSERT(liveDev->setFilter(tcpFlagsFilter), "Could not set filter: %s", filterAsString.c_str());
    PCAPP_ASSERT(liveDev->startCapture(capturedPackets), "Cannot start capture for filter '%s'", filterAsString.c_str());
	sendURLRequest("www.cnn.com");
	PCAP_SLEEP(5);
	liveDev->stopCapture();
	PCAPP_ASSERT(capturedPackets.size() > 0, "TcpFlagsFilter test #1: Captured 0 packets");
    for (RawPacketVector::VectorIterator iter = capturedPackets.begin(); iter != capturedPackets.end(); iter++)
    {
    	Packet packet(*iter);
    	PCAPP_ASSERT(packet.isPacketOfType(TCP), "TcpFlagsFilter test #1: one of the captured packets isn't of type TCP");
    	TcpLayer* tcpLayer = packet.getLayerOfType<TcpLayer>();
    	PCAPP_ASSERT(tcpLayer->getTcpHeader()->synFlag == 1 && tcpLayer->getTcpHeader()->ackFlag == 1, "TcpFlagsFilter test #1: TCP packet isn't a SYN/ACK packet");
    }

    capturedPackets.clear();
    tcpFlagsFilter.setTcpFlagsBitMask(tcpFlagsBitMask, TcpFlagsFilter::MatchOneAtLeast);
    tcpFlagsFilter.parseToString(filterAsString);
    PCAPP_ASSERT(liveDev->setFilter(tcpFlagsFilter), "Could not set filter: %s", filterAsString.c_str());
    PCAPP_ASSERT(liveDev->startCapture(capturedPackets), "Cannot start capture for filter '%s'", filterAsString.c_str());
	sendURLRequest("www.bbc.com");
	PCAP_SLEEP(5);
	liveDev->stopCapture();
	PCAPP_ASSERT(capturedPackets.size() > 0, "TcpFlagsFilter test #2: Captured 0 packets");
    for (RawPacketVector::VectorIterator iter = capturedPackets.begin(); iter != capturedPackets.end(); iter++)
    {
    	Packet packet(*iter);
    	PCAPP_ASSERT(packet.isPacketOfType(TCP), "TcpFlagsFilter test #2: one of the captured packets isn't of type TCP");
    	TcpLayer* tcpLayer = packet.getLayerOfType<TcpLayer>();
    	PCAPP_ASSERT(tcpLayer->getTcpHeader()->synFlag == 1 || tcpLayer->getTcpHeader()->ackFlag == 1, "TcpFlagsFilter test #2: TCP packet isn't a SYN or ACK packet");
    }

    capturedPackets.clear();

    //-------------------------
    //IP filter with mask
    //-------------------------
    IPFilter ipFilterWithMask("212.199.202.9", SRC, "255.255.255.0");
    ipFilterWithMask.parseToString(filterAsString);
    PCAPP_ASSERT(liveDev->setFilter(ipFilterWithMask), "Could not set filter: %s", filterAsString.c_str());
    PCAPP_ASSERT(liveDev->startCapture(capturedPackets), "Cannot start capture for filter '%s'", filterAsString.c_str());
    PCAPP_ASSERT(fileReaderDev2.open(), "Cannot open file reader device for filter '%s'", filterAsString.c_str());
    while (fileReaderDev2.getNextPacket(rawPacket))
    {
    	PCAPP_ASSERT(liveDev->sendPacket(rawPacket), "Could not send packet. Testing filter: '%s'", filterAsString.c_str());
    }
    fileReaderDev2.close();
    liveDev->stopCapture();
    PCAPP_ASSERT(capturedPackets.size() >= 2536, "IPFilter with mask test: Captured less than %d packets", 2536);
    for (RawPacketVector::VectorIterator iter = capturedPackets.begin(); iter != capturedPackets.end(); iter++)
    {
    	Packet packet(*iter);
    	PCAPP_ASSERT(packet.isPacketOfType(IPv4), "IPFilter with mask test: one of the captured packets isn't of type IPv4");
    	IPv4Layer* ipLayer = packet.getLayerOfType<IPv4Layer>();
		PCAPP_ASSERT(ipLayer->getSrcIpAddress().matchSubnet(IPv4Address(string("212.199.202.9")), "255.255.255.0"), "IPFilter with mask test: packet doesn't match subnet mask. IP src: '%s'", ipLayer->getSrcIpAddress().toString().c_str());
    }

    capturedPackets.clear();

    ipFilterWithMask.setLen(24);
    ipFilterWithMask.setAddr("212.199.202.9");
    ipFilterWithMask.parseToString(filterAsString);
    PCAPP_ASSERT(liveDev->setFilter(ipFilterWithMask), "Could not set filter: %s", filterAsString.c_str());
    PCAPP_ASSERT(liveDev->startCapture(capturedPackets), "Cannot start capture for filter '%s'", filterAsString.c_str());
    PCAPP_ASSERT(fileReaderDev2.open(), "Cannot open file reader device for filter '%s'", filterAsString.c_str());
    while (fileReaderDev2.getNextPacket(rawPacket))
    {
    	PCAPP_ASSERT(liveDev->sendPacket(rawPacket), "Could not send packet. Testing filter: '%s'", filterAsString.c_str());
    }
    fileReaderDev2.close();
    liveDev->stopCapture();
    PCAPP_ASSERT(capturedPackets.size() >= 2536, "IPFilter with mask test #2: Captured less than %d packets", 2536);
    for (RawPacketVector::VectorIterator iter = capturedPackets.begin(); iter != capturedPackets.end(); iter++)
    {
    	Packet packet(*iter);
    	PCAPP_ASSERT(packet.isPacketOfType(IPv4), "IPFilter with mask test #2: one of the captured packets isn't of type IPv4");
    	IPv4Layer* ipLayer = packet.getLayerOfType<IPv4Layer>();
		PCAPP_ASSERT(ipLayer->getSrcIpAddress().matchSubnet(IPv4Address(string("212.199.202.9")), "255.255.255.0"), "IPFilter with mask test: packet doesn't match subnet mask. IP src: '%s'", ipLayer->getSrcIpAddress().toString().c_str());
    }
    capturedPackets.clear();

    //-------------
    //Port range
    //-------------
    PortRangeFilter portRangeFilter(40000, 50000, SRC);
    portRangeFilter.parseToString(filterAsString);
    PCAPP_ASSERT(liveDev->setFilter(portRangeFilter), "Could not set filter: %s", filterAsString.c_str());
    PCAPP_ASSERT(liveDev->startCapture(capturedPackets), "Cannot start capture for filter '%s'", filterAsString.c_str());
    PCAPP_ASSERT(fileReaderDev2.open(), "Cannot open file reader device for filter '%s'", filterAsString.c_str());
    while (fileReaderDev2.getNextPacket(rawPacket))
    {
    	PCAPP_ASSERT(liveDev->sendPacket(rawPacket), "Could not send packet. Testing filter: '%s'", filterAsString.c_str());
    }
    fileReaderDev2.close();
    liveDev->stopCapture();
    PCAPP_ASSERT(capturedPackets.size() >= 1464, "PortRangeFilter: Captured less than %d packets", 1899);

    for (RawPacketVector::VectorIterator iter = capturedPackets.begin(); iter != capturedPackets.end(); iter++)
    {
    	Packet packet(*iter);
    	PCAPP_ASSERT(packet.isPacketOfType(TCP) || packet.isPacketOfType(UDP), "PortRangeFilter: one of the captured packets isn't of type TCP or UDP");
    	if (packet.isPacketOfType(TCP))
    	{
    		TcpLayer* tcpLayer = packet.getLayerOfType<TcpLayer>();
    		uint16_t portSrc = ntohs(tcpLayer->getTcpHeader()->portSrc);
    		PCAPP_ASSERT(portSrc >= 40000 && portSrc <=50000, "PortRangeFilter: TCP packet source port is out of range (40000-50000). Src port: %d", portSrc);
    	}
    	else if (packet.isPacketOfType(UDP))
    	{
    		UdpLayer* udpLayer = packet.getLayerOfType<UdpLayer>();
    		uint16_t portSrc = ntohs(udpLayer->getUdpHeader()->portSrc);
    		PCAPP_ASSERT(portSrc >= 40000 && portSrc <=50000, "PortRangeFilter: UDP packet source port is out of range (40000-50000). Src port: %d", portSrc);
    	}
    }
    capturedPackets.clear();

    liveDev->close();
	PCAPP_TEST_PASSED;
}

PCAPP_TEST(TestSendPacket)
{
	PcapLiveDevice* liveDev = NULL;
	IPv4Address ipToSearch(args.ipToSendReceivePackets.c_str());
	liveDev = PcapLiveDeviceList::getInstance().getPcapLiveDeviceByIp(ipToSearch);
    PCAPP_ASSERT(liveDev != NULL, "Device used in this test %s doesn't exist", args.ipToSendReceivePackets.c_str());
    PCAPP_ASSERT(liveDev->open(), "Cannot open live device");

    PcapFileReaderDevice fileReaderDev(EXAMPLE_PCAP_PATH);
    PCAPP_ASSERT(fileReaderDev.open(), "Cannot open file reader device");

    PCAPP_ASSERT(liveDev->getMtu() > 0, "Could not get live device MTU");
    uint16_t mtu = liveDev->getMtu();
    int buffLen = mtu+1;
    uint8_t buff[buffLen];
    memset(buff, 0, buffLen);
    PCAPP_ASSERT(!liveDev->sendPacket(buff, buffLen), "Defected packet was sent successfully");

    RawPacket rawPacket;
    int packetsSent = 0;
    int packetsRead = 0;
    while(fileReaderDev.getNextPacket(rawPacket))
    {
    	packetsRead++;

    	//send packet as RawPacket
    	PCAPP_ASSERT(liveDev->sendPacket(rawPacket), "Could not send raw packet");

    	//send packet as raw data
    	PCAPP_ASSERT(liveDev->sendPacket(rawPacket.getRawData(), rawPacket.getRawDataLen()), "Could not send raw data");

    	//send packet as parsed EthPacekt
    	Packet packet(&rawPacket);
    	PCAPP_ASSERT(liveDev->sendPacket(&packet), "Could not send parsed packet");

   		packetsSent++;
    }

    PCAPP_ASSERT(packetsRead == packetsSent, "Unexpected number of packets sent. Expected (read from file): %d; Sent: %d", packetsRead, packetsSent);

    liveDev->close();
    fileReaderDev.close();

    PCAPP_TEST_PASSED;
}

PCAPP_TEST(TestSendPackets)
{
	PcapLiveDevice* liveDev = NULL;
	IPv4Address ipToSearch(args.ipToSendReceivePackets.c_str());
	liveDev = PcapLiveDeviceList::getInstance().getPcapLiveDeviceByIp(ipToSearch);
    PCAPP_ASSERT(liveDev != NULL, "Device used in this test %s doesn't exist", args.ipToSendReceivePackets.c_str());
    PCAPP_ASSERT(liveDev->open(), "Cannot open live device");

    PcapFileReaderDevice fileReaderDev(EXAMPLE_PCAP_PATH);
    PCAPP_ASSERT(fileReaderDev.open(), "Cannot open file reader device");

    RawPacket rawPacketArr[10000];
    PointerVector<Packet> packetVec;
    Packet* packetArr[10000];
    int packetsRead = 0;
    while(fileReaderDev.getNextPacket(rawPacketArr[packetsRead]))
    {
    	packetVec.pushBack(new Packet(&rawPacketArr[packetsRead]));
    	packetsRead++;
    }

    //send packets as RawPacket array
    int packetsSentAsRaw = liveDev->sendPackets(rawPacketArr, packetsRead);

    //send packets as parsed EthPacekt array
    std::copy(packetVec.begin(), packetVec.end(), packetArr);
    int packetsSentAsParsed = liveDev->sendPackets(packetArr, packetsRead);

    PCAPP_ASSERT(packetsSentAsRaw == packetsRead, "Not all packets were sent as raw. Expected (read from file): %d; Sent: %d", packetsRead, packetsSentAsRaw);
    PCAPP_ASSERT(packetsSentAsParsed == packetsRead, "Not all packets were sent as parsed. Expected (read from file): %d; Sent: %d", packetsRead, packetsSentAsParsed);

    liveDev->close();
    fileReaderDev.close();

    PCAPP_TEST_PASSED;
}

PCAPP_TEST(TestRemoteCapture)
{
#ifdef WIN32
	PcapRemoteDeviceList remoteDevices;
	bool useRemoteDevicesFromArgs = (args.remoteIp != "") && (args.remotePort > 0);
	string remoteDeviceIP = (useRemoteDevicesFromArgs ? args.remoteIp : args.ipToSendReceivePackets);
	uint16_t remoteDevicePort = (useRemoteDevicesFromArgs ? args.remotePort : 12321);

	HANDLE rpcapdHandle = NULL;
	if (!useRemoteDevicesFromArgs)
	{
		rpcapdHandle = activateRpcapdServer(remoteDeviceIP, remoteDevicePort);
		PCAPP_ASSERT(rpcapdHandle != NULL, "Could not create rpcapd process. Error was: %lu", GetLastError());

	}

	PCAPP_ASSERT_AND_RUN_COMMAND(PcapRemoteDeviceList::getRemoteDeviceList(remoteDeviceIP, remoteDevicePort, remoteDevices), terminateRpcapdServer(rpcapdHandle), "Error on retrieving remote devices on IP: %s port: %d. Error string was: %s", remoteDeviceIP.c_str(), remoteDevicePort, args.errString);
	PcapRemoteDevice* pRemoteDevice = remoteDevices.getRemoteDeviceByIP(remoteDeviceIP.c_str());
	PCAPP_ASSERT_AND_RUN_COMMAND(pRemoteDevice->open(), terminateRpcapdServer(rpcapdHandle), "Could not open the remote device. Error was: %s", args.errString);
	RawPacketVector capturedPackets;
	PCAPP_ASSERT_AND_RUN_COMMAND(pRemoteDevice->startCapture(capturedPackets), terminateRpcapdServer(rpcapdHandle), "Couldn't start capturing on remote device '%s'. Error was: %s", pRemoteDevice->getName(), args.errString);

	if (!useRemoteDevicesFromArgs)
		PCAPP_ASSERT_AND_RUN_COMMAND(sendURLRequest("www.yahoo.com"), terminateRpcapdServer(rpcapdHandle), "Couldn't send URL");

	PCAP_SLEEP(20);
	pRemoteDevice->stopCapture();

	//send single packet
	PCAPP_ASSERT_AND_RUN_COMMAND(pRemoteDevice->sendPacket(*capturedPackets.front()), terminateRpcapdServer(rpcapdHandle), "Couldn't send a packet. Error was: %s", args.errString);

	//send multiple packets
	RawPacketVector packetsToSend;
	vector<RawPacket*>::iterator iter = capturedPackets.begin();

	size_t capturedPacketsSize = capturedPackets.size();
	while (iter != capturedPackets.end())
	{
		if ((*iter)->getRawDataLen() <= pRemoteDevice->getMtu())
		{
			packetsToSend.pushBack(capturedPackets.getAndRemoveFromVector(iter));
		}
		else
			++iter;
	}
	int packetsSent = pRemoteDevice->sendPackets(packetsToSend);
	PCAPP_ASSERT_AND_RUN_COMMAND(packetsSent == (int)packetsToSend.size(), terminateRpcapdServer(rpcapdHandle), "%d packets sent out of %d. Error was: %s", packetsSent, packetsToSend.size(), args.errString);

	//check statistics
	pcap_stat stats;
	pRemoteDevice->getStatistics(stats);
	PCAPP_ASSERT_AND_RUN_COMMAND(stats.ps_recv == capturedPacketsSize, terminateRpcapdServer(rpcapdHandle),
			"Statistics returned from rpcapd doesn't equal the captured packets vector size. Stats: %d; Vector size: %d",
			stats.ps_recv, capturedPacketsSize);

	pRemoteDevice->close();

	terminateRpcapdServer(rpcapdHandle);
#endif

	PCAPP_TEST_PASSED;
}

PCAPP_TEST(TestHttpRequestParsing)
{
    PcapFileReaderDevice readerDev(EXAMPLE_PCAP_HTTP_REQUEST);
    PCAPP_ASSERT(readerDev.open(), "cannot open reader device");

    RawPacket rawPacket;
    int packetCount = 0;

    int httpPackets = 0;
    int getReqs = 0;
    int postReqs = 0;
    int headReqs = 0;
    int optionsReqs = 0;
    int otherMethodReqs = 0;

    int swfReqs = 0;
    int homeReqs = 0;

    int winwinReqs = 0;
    int yad2Reqs = 0;
    int wcdnReqs = 0;

    int ieReqs = 0;
    int ffReqs = 0;
    int chromeReqs = 0;

    while (readerDev.getNextPacket(rawPacket))
    {
    	packetCount++;
    	Packet packet(&rawPacket);
		if (packet.isPacketOfType(HTTPRequest))
			httpPackets++;
		else
			continue;

		HttpRequestLayer* httpReqLayer = packet.getLayerOfType<HttpRequestLayer>();
		PCAPP_ASSERT(httpReqLayer->getFirstLine() != NULL, "HTTP first line is null in packet #%d, HTTP request #%d", packetCount, httpPackets);
		switch (httpReqLayer->getFirstLine()->getMethod())
		{
		case HttpRequestLayer::HttpGET:
			getReqs++;
			break;
		case HttpRequestLayer::HttpPOST:
			postReqs++;
			break;
		case HttpRequestLayer::HttpOPTIONS:
			optionsReqs++;
			break;
		case HttpRequestLayer::HttpHEAD:
			headReqs++;
			break;
		default:
			otherMethodReqs++;
		}


		if (httpReqLayer->getFirstLine()->isComplete())
		{
			PCAPP_ASSERT(httpReqLayer->getFirstLine()->getVersion() == OneDotOne, "HTTP version is different than 1.1 in packet #%d, HTTP request #%d", packetCount, httpPackets);
		}

		if (httpReqLayer->getFirstLine()->getUri().find(".swf") != std::string::npos)
			swfReqs++;
		else if (httpReqLayer->getFirstLine()->getUri().find("home") != std::string::npos)
			homeReqs++;

		HttpField* hostField = httpReqLayer->getFieldByName("Host");
		if (hostField != NULL)
		{
			std::string host = hostField->getFieldValue();
			if (host == "www.winwin.co.il")
				winwinReqs++;
			else if (host == "www.yad2.co.il")
				yad2Reqs++;
			else if (host == "msc.wcdn.co.il")
				wcdnReqs++;
		}

		HttpField* userAgentField = httpReqLayer->getFieldByName("User-Agent");
		if (userAgentField == NULL)
			continue;

		std::string userAgent = userAgentField->getFieldValue();
		if (userAgent.find("Trident/7.0") != std::string::npos)
			ieReqs++;
		else if (userAgent.find("Firefox/33.0") != std::string::npos)
			ffReqs++;
		else if (userAgent.find("Chrome/38.0") != std::string::npos)
			chromeReqs++;
    }

    readerDev.close();

//    printf("packetCount: %d (7299)\n", packetCount);
//    printf("httpPackets: %d (3579)\n", httpPackets);
//    printf("otherMethodReqs: %d (0)\n", otherMethodReqs);
//    printf("getReqs: %d (3411)\n", getReqs);
//    printf("postReqs: %d (156)\n", postReqs);
//    printf("optionsReqs: %d (7)\n", optionsReqs);
//    printf("headReqs: %d (5)\n", headReqs);
//    printf("homeReqs: %d (118)\n", homeReqs);
//    printf("swfReqs: %d (74)\n", swfReqs);
//    printf("wcdnReqs: %d (20)\n", wcdnReqs);
//    printf("yad2Reqs: %d (102)\n", yad2Reqs);
//    printf("winwinReqs: %d (306)\n", winwinReqs);
//    printf("ieReqs: %d (719)\n", ieReqs);
//    printf("ffReqs: %d (1053)\n", ffReqs);
//    printf("chromeReqs: %d (1702)\n", chromeReqs);


    PCAPP_ASSERT(packetCount == 7299, "Packet count is wrong. Actual: %d; Expected: %d", packetCount, 7299);

    // Wireshark filter: (tcp.dstport == 80 || tcp.dstport == 8080) && (tcp contains "GET " || tcp contains "POST " || tcp contains "HEAD " || tcp contains "OPTIONS ")
    PCAPP_ASSERT(httpPackets == 3579, "HTTP packet count is wrong. Actual: %d; Expected: %d", httpPackets, 3579);


    PCAPP_ASSERT(otherMethodReqs == 0, "Parsed %d HTTP requests with unexpected method", otherMethodReqs);

    // Wireshark filter: (tcp.dstport == 80 || tcp.dstport == 8080) && (tcp contains "GET ")
    PCAPP_ASSERT(getReqs == 3411, "Number of GET requests different than expected. Actual: %d; Expected: %d", getReqs, 3411);
    // Wireshark filter: (tcp.dstport == 80 || tcp.dstport == 8080) && (tcp contains "POST ")
    PCAPP_ASSERT(postReqs == 156, "Number of POST requests different than expected. Actual: %d; Expected: %d", postReqs, 156);
    // Wireshark filter: (tcp.dstport == 80 || tcp.dstport == 8080) && (tcp contains "OPTIONS ")
    PCAPP_ASSERT(optionsReqs == 7, "Number of OPTIONS requests different than expected. Actual: %d; Expected: %d", optionsReqs, 7);
    // Wireshark filter: (tcp.dstport == 80 || tcp.dstport == 8080) && (tcp contains "HEAD ")
    PCAPP_ASSERT(headReqs == 5, "Number of HEAD requests different than expected. Actual: %d; Expected: %d", headReqs, 5);


    // Wireshark filter: (tcp.dstport == 80 || tcp.dstport == 8080) && (tcp contains "GET " || tcp contains "POST ") && (tcp matches "home.*HTTP/1.1")
    PCAPP_ASSERT(homeReqs == 118, "Number of requests with URI contains 'home' is different than expected. Actual: %d; Expected: %d", homeReqs, 118);
    // Wireshark filter: http.request.full_uri contains .swf
    PCAPP_ASSERT(swfReqs == 74, "Number of requests with URI contains '.swf' is different than expected. Actual: %d; Expected: %d", swfReqs, 74);

    // Wireshark filter: http.host == msc.wcdn.co.il
    PCAPP_ASSERT(wcdnReqs == 20, "Number of requests from msc.wcdn.co.il is different than expected. Actual: %d; Expected: %d", wcdnReqs, 20);
    // Wireshark filter: http.host == www.yad2.co.il
    PCAPP_ASSERT(yad2Reqs == 102, "Number of requests from www.yad2.co.il is different than expected. Actual: %d; Expected: %d", yad2Reqs, 102);
    // Wireshark filter: http.host == www.winwin.co.il
    PCAPP_ASSERT(winwinReqs == 306, "Number of requests from www.winwin.co.il is different than expected. Actual: %d; Expected: %d", winwinReqs, 306);


    // Wireshark filter: (tcp.dstport == 80 || tcp.dstport == 8080) && (tcp contains "GET " || tcp contains "POST " || tcp contains "HEAD " || tcp contains "OPTIONS ") && (tcp contains "Firefox/33.0")
    PCAPP_ASSERT(ffReqs == 1053, "Number of Firefox requests is different than expected. Actual: %d; Expected: %d", ffReqs, 1053);
    // Wireshark filter: (tcp.dstport == 80 || tcp.dstport == 8080) && (tcp contains "GET " || tcp contains "POST " || tcp contains "HEAD " || tcp contains "OPTIONS ") && (tcp contains "Chrome/38.0")
    PCAPP_ASSERT(chromeReqs == 1702, "Number of Chrome requests is different than expected. Actual: %d; Expected: %d", chromeReqs, 1702);
    // Wireshark filter: (tcp.dstport == 80 || tcp.dstport == 8080) && (tcp contains "GET " || tcp contains "POST " || tcp contains "HEAD " || tcp contains "OPTIONS ") && (tcp contains "Trident/7.0")
    PCAPP_ASSERT(ieReqs == 719, "Number of IE requests is different than expected. Actual: %d; Expected: %d", ieReqs, 719);

	PCAPP_TEST_PASSED;
}

PCAPP_TEST(TestHttpResponseParsing)
{
    PcapFileReaderDevice readerDev(EXAMPLE_PCAP_HTTP_RESPONSE);
    PCAPP_ASSERT(readerDev.open(), "cannot open reader device");

    RawPacket rawPacket;
    int packetCount = 0;
    int httpResponsePackets = 0;

	int statusCodes[80];

	int textHtmlCount = 0;
	int imageCount = 0;
	int gzipCount = 0;
	int chunkedCount = 0;

	int bigResponses = 0;

	memset(statusCodes, 0, 80*sizeof(int));

    while (readerDev.getNextPacket(rawPacket))
    {
    	packetCount++;
    	Packet packet(&rawPacket);
		if (packet.isPacketOfType(HTTPResponse))
			httpResponsePackets++;
		else
			continue;

		HttpResponseLayer* httpResLayer = packet.getLayerOfType<HttpResponseLayer>();
		PCAPP_ASSERT(httpResLayer->getFirstLine() != NULL, "HTTP first line is null in packet #%d, HTTP request #%d", packetCount, httpResponsePackets);
		statusCodes[httpResLayer->getFirstLine()->getStatusCode()]++;

		HttpField* contentTypeField = httpResLayer->getFieldByName(HTTP_CONTENT_TYPE_FIELD);
		if (contentTypeField != NULL)
		{
			std::string contentType = contentTypeField->getFieldValue();
			if (contentType.find("image/") != std::string::npos)
				imageCount++;
			else if (contentType == "text/html")
				textHtmlCount++;
		}

		HttpField* contentEncodingField = httpResLayer->getFieldByName(HTTP_CONTENT_ENCODING_FIELD);
		if (contentEncodingField != NULL && contentEncodingField->getFieldValue() == "gzip")
			gzipCount++;

		HttpField* transferEncodingField = httpResLayer->getFieldByName(HTTP_TRANSFER_ENCODING_FIELD);
		if (transferEncodingField != NULL && transferEncodingField->getFieldValue() == "chunked")
			chunkedCount++;

		HttpField* contentLengthField = httpResLayer->getFieldByName(HTTP_CONTENT_LENGTH_FIELD);
		if (contentLengthField != NULL)
		{
			std::string lengthAsString = contentLengthField->getFieldValue();
			int length = atoi(lengthAsString.c_str());
			if (length > 100000)
				bigResponses++;
		}


    }

    PCAPP_ASSERT(packetCount == 7435, "Packet count is different than expected. Found: %d; Expected: 7435", packetCount);

    // *** wireshark has a bug there and displays 1 less packet as http response. Missing packet IP ID is 10419 ***
    // ************************************************************************************************************

    // wireshark filter: http.response && (tcp.srcport == 80 || tcp.srcport == 8080)
    PCAPP_ASSERT(httpResponsePackets == 682, "HTTP response count is different than expected. Found: %d; Expected: 682", httpResponsePackets);
    // wireshark filter: http.response && (tcp.srcport == 80 || tcp.srcport == 8080) && http.response.code == 200
    PCAPP_ASSERT(statusCodes[HttpResponseLayer::Http200OK] == 592, "HTTP response with 200 OK count is different than expected. Found: %d; Expected: 592", statusCodes[HttpResponseLayer::Http200OK]);
    // wireshark filter: http.response && (tcp.srcport == 80 || tcp.srcport == 8080) && http.response.code == 302
    PCAPP_ASSERT(statusCodes[HttpResponseLayer::Http302] == 15, "HTTP response with 302 count is different than expected. Found: %d; Expected: 15", statusCodes[HttpResponseLayer::Http302]);
    // wireshark filter: http.response && (tcp.srcport == 80 || tcp.srcport == 8080) && http.response.code == 304
    PCAPP_ASSERT(statusCodes[HttpResponseLayer::Http304NotModified] == 26, "HTTP response with 304 count is different than expected. Found: %d; Expected: 26", statusCodes[HttpResponseLayer::Http304NotModified]);

    // wireshark filter: http.response && (tcp.srcport == 80 || tcp.srcport == 8080) && http.content_type == "text/html"
    PCAPP_ASSERT(textHtmlCount == 38, "HTTP responses with content-type=='text/html' is different than expected. Expected: %d; Actual: %d", 38, textHtmlCount);
    // wireshark filter: http.response && (tcp.srcport == 80 || tcp.srcport == 8080) && http.content_type contains "image/"
    PCAPP_ASSERT(imageCount == 369, "HTTP responses with content-type=='image/*' is different than expected. Expected: %d; Actual: %d", 369, imageCount);

    // wireshark filter: (tcp.srcport == 80 || tcp.srcport == 8080) && tcp contains "HTTP/1." && (tcp contains "Transfer-Encoding:  chunked" || tcp contains "Transfer-Encoding: chunked" || tcp contains "transfer-encoding: chunked")
    PCAPP_ASSERT(chunkedCount == 45, "HTTP responses with transfer-encoding=='chunked' is different than expected. Expected: %d; Actual: %d", 45, chunkedCount);
    // wireshark filter: (tcp.srcport == 80 || tcp.srcport == 8080) && tcp contains "HTTP/1." && tcp contains "Content-Encoding: gzip"
    PCAPP_ASSERT(gzipCount == 148, "HTTP responses with content-encoding=='gzip' is different than expected. Expected: %d; Actual: %d", 148, gzipCount);

    // wireshark filter: http.content_length > 100000
    PCAPP_ASSERT(bigResponses == 14, "HTTP responses with content-length > 100K is different than expected. Expected: %d; Actual: %d", 14, bigResponses);

//    printf("Total HTTP response packets: %d\n", httpResponsePackets);
//    printf("200 OK packets: %d\n", statusCodes[HttpResponseLayer::Http200OK]);
//    printf("302 packets: %d\n", statusCodes[HttpResponseLayer::Http302]);
//    printf("304 Not Modified packets: %d\n", statusCodes[HttpResponseLayer::Http304NotModified]);
//    printf("text/html responses: %d\n", textHtmlCount);
//    printf("image responses: %d\n", imageCount);
//    printf("gzip responses: %d\n", gzipCount);
//    printf("chunked responses: %d\n", chunkedCount);
//    printf("big responses: %d\n", bigResponses);

	PCAPP_TEST_PASSED;
}

PCAPP_TEST(TestPrintPacketAndLayers)
{
	PcapFileReaderDevice reader(EXAMPLE2_PCAP_PATH);
	PCAPP_ASSERT(reader.open(), "Cannot open reader device for '%s'", EXAMPLE2_PCAP_PATH);
	RawPacket rawPacket;
	ostringstream outputStream;
	while (reader.getNextPacket(rawPacket))
	{
		Packet packet(&rawPacket);
		outputStream << packet.printToString() << "\n\n";
	}

	ifstream referenceFile("PcapExamples/example2_summary.txt");
	stringstream referenceBuffer;
	referenceBuffer << referenceFile.rdbuf();
	referenceFile.close();

	// example2_summary.txt was written with Windows so every '\n' is translated to '\r\n'
	// in Linux '\n' stays '\n' in writing to files. So these lines of code are meant to remove the '\r' so
	// files can be later compared
	std::string referenceBufferAsString = referenceBuffer.str();
	size_t index = 0;
	while (true) {
	     index = referenceBufferAsString.find("\r\n", index);
	     if (index == string::npos) break;
	     referenceBufferAsString.replace(index, 2, "\n");
	     index += 1;
	}

	PCAPP_ASSERT(referenceBufferAsString == outputStream.str(), "Output is different than reference file");

	PCAPP_TEST_PASSED;
}



static struct option PcapTestOptions[] =
{
	{"debug-mode", no_argument, 0, 'd'},
	{"use-ip",  required_argument, 0, 'i'},
	{"remote-ip", required_argument, 0, 'r'},
	{"remote-port", required_argument, 0, 'p'},
    {0, 0, 0, 0}
};

void print_usage() {
    printf("Usage: Pcap++Test -i IP_TO_USE\n\n"
    		"Flags:\n"
    		"-i --use-ip		IP to use for sending and receiving packets\n"
    		"-d --debug-mode		Set log level to DEBUG\n"
    		"-r --remote-ip		IP of remote machine running rpcapd to test remote capture\n"
    		"-p --remote-port	Port of remote machine running rpcapd to test remote capture\n");
}

int main(int argc, char* argv[])
{
	start_leak_check();
	PcapTestArgs args;
	args.ipToSendReceivePackets = "";
	args.debugMode = false;

	int optionIndex = 0;
	char opt = 0;
	while((opt = getopt_long (argc, argv, "di:r:p:", PcapTestOptions, &optionIndex)) != -1)
	{
		switch (opt)
		{
			case 0:
				break;
			case 'i':
				args.ipToSendReceivePackets = optarg;
				break;
			case 'd':
				args.debugMode = true;
				break;
			case 'r':
				args.remoteIp = optarg;
				break;
			case 'p':
				args.remotePort = (uint16_t)atoi(optarg);
				break;
			default:
				print_usage();
				exit(-1);
		}
	}

	if(args.ipToSendReceivePackets == "")
	{
		print_usage();
		exit(-1);
	}

	if (args.debugMode)
		LoggerPP::getInstance().setAllModlesToLogLevel(LoggerPP::Debug);

	printf("Using ip: %s\n", args.ipToSendReceivePackets.c_str());
	printf("Debug mode: %s\n", args.debugMode ? "on" : "off");
	printf("Starting tests...\n");

	char errString[1000];
	LoggerPP::getInstance().setErrorString(errString, 1000);
	args.errString = errString;

	PCAPP_START_RUNNING_TESTS;

	PCAPP_RUN_TEST(TestIPAddress, args);
	PCAPP_RUN_TEST(TestMacAddress, args);
	PCAPP_RUN_TEST(TestPcapFileReadWrite, args);
	PCAPP_RUN_TEST(TestPcapLiveDeviceList, args);
	PCAPP_RUN_TEST(TestPcapLiveDeviceListSearch, args);
	PCAPP_RUN_TEST(TestPcapLiveDevice, args);
	PCAPP_RUN_TEST(TestPcapLiveDeviceStatsMode, args);
	PCAPP_RUN_TEST(TestWinPcapLiveDevice, args);
	PCAPP_RUN_TEST(TestPcapFilters, args);
	PCAPP_RUN_TEST(TestSendPacket, args);
	PCAPP_RUN_TEST(TestSendPackets, args);
	PCAPP_RUN_TEST(TestRemoteCapture, args);
	PCAPP_RUN_TEST(TestHttpRequestParsing, args);
	PCAPP_RUN_TEST(TestHttpResponseParsing, args);
	PCAPP_RUN_TEST(TestPrintPacketAndLayers, args);

	PCAPP_END_RUNNING_TESTS;
}
