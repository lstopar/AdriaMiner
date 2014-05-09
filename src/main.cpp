#include "adria_server.h"

PNotify Notify = TStdNotify::New();
TAdriaComm::PAdriaServer AdriaServer;

bool StopSrv() {
	Notify->OnNotify(TNotifyType::ntInfo, "Stopping server...");

	AdriaServer->ShutDown();

	TLoop::Stop();

	Notify->OnNotify(TNotifyType::ntInfo, "Server stopped!");

	return true;
}

void BuildUnicode() {
	printf("Building unicode DB...\n");
	TUniChDb ChDb;
	ChDb.LoadTxt("/home/pi/adria/miner/glib/bin/UnicodeDefTxt");
	ChDb.SaveBin("/home/pi/UnicodeDef.Bin");
	printf("Unicode complete!\n");
}


int main(int argc, char* argv[]) {
#ifndef NDEBUG
    // report we are running with all Asserts turned on
    printf("*** Running in debug mode ***\n");
    setbuf(stdout, NULL);
#endif    
    try {
    	Env = TEnv(argc, argv, Notify);
		Env.SetNoLine(); // making output prettier

		const int PortN = Env.GetIfArgPrefixInt("-port=", 8080, "Port number");
		const TStr HostNm = Env.GetIfArgPrefixStr("-host=", "127.0.0.1", "Host");

    	// start server
    	Notify->OnNotifyFmt(TNotifyType::ntInfo, "Starting socket client, host: %s, port %d", HostNm.CStr(), PortN);

    	TAdriaComm::TDataProvider DataProvider(Notify);
		AdriaServer = TAdriaComm::TAdriaServer::New(TAdriaComm::TAdriaCommunicator::New(HostNm, PortN, Notify), DataProvider, Notify);

		TLoop::Ref();
		TLoop::Run();
    } catch (const PExcept& Except) {
    	Notify->OnNotify(TNotifyType::ntErr, Except->GetMsgStr());
    	return 1;
    } catch (...) {
    	Notify->OnNotify(TNotifyType::ntErr, "Unknown exception, exiting...");
    	return 2;
    }

    Notify->OnNotify(TNotifyType::ntInfo, "Shut down!");

    return 0;
}
