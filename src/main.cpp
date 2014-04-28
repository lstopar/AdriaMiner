#include "adria_server.h"

const TStr TempLogFName = "unsaved.log";

PNotify Notify = TStdNotify::New();
TAdriaComm::PAdriaServer AdriaServer;

bool StopSrv() {
	Notify->OnNotify(TNotifyType::ntInfo, "Stopping server...");

	AdriaServer->ShutDown();

	TLoop::Stop();

	Notify->OnNotify(TNotifyType::ntInfo, "Server stopped!");

	return true;
}

void HandleSignal(int Sig) {
	printf("Received signal: %d", Sig);

	switch (Sig) {
	case SIGTERM: {
		StopSrv();
		break;
	case SIGINT: {
		StopSrv();
		break;
	} case SIGHUP: {
		StopSrv();
		break;
	} case SIGSTOP: {
		StopSrv();
		break;
	} default:
		Notify->OnNotifyFmt(TNotifyType::ntInfo, "Received unknown signal: %d", Sig);
	}
	}
}

void RegSigHandlers(const PNotify& Notify) {

	// register signal handlers
	Notify->OnNotify(TNotifyType::ntInfo, "Registering signal handlers...");

	signal(SIGTERM, HandleSignal);
	signal(SIGINT, HandleSignal);
	signal(SIGHUP, HandleSignal);
	signal(SIGSTOP, HandleSignal);
}

void BuildUnicode() {
	printf("Building unicode DB...\n");
	TUniChDb ChDb;
	ChDb.LoadTxt("/home/lstopar/workspace/JSI/qminer/glib/bin/UnicodeDefTxt");
	ChDb.SaveBin("/home/lstopar/workspace/JSI/qminer/UnicodeDef.Bin");
	printf("Unicode complete!\n");
}


int main(int argc, char* argv[]) {
#ifndef NDEBUG
    // report we are running with all Asserts turned on
    printf("*** Running in debug mode ***\n");
    setbuf(stdout, NULL);
#endif    
    try {
    	TStr HostNm = "127.0.0.1";
    	int Port = 9999;
    	TStr DbFNm = "./db/";

    	// start server
    	Notify->OnNotifyFmt(TNotifyType::ntInfo, "Starting socket client, host: %s, port %d", HostNm.CStr(), Port);

		AdriaServer = TAdriaComm::TAdriaServer::New(TAdriaComm::TAdriaCommunicator::New(HostNm, Port, Notify), TAdriaComm::TDataProvider(DbFNm, Notify), Notify);

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