#include "utils.h"

using namespace TAdriaUtils;

////////////////////////////////////////////////////
// TUtils
const int TUtils::FreshWaterCanId = 108;


////////////////////////////////////////////////////
// TAdriaMsg
const TChA TAdriaMsg::POST = "POST";
const TChA TAdriaMsg::PUSH = "PUSH";
const TChA TAdriaMsg::GET = "GET";

const TChA TAdriaMsg::RES_TABLE = "res_table";
const TChA TAdriaMsg::HISTORY = "history";
const TChA TAdriaMsg::PREDICTION = "prediction";

const int TAdriaMsg::BYTES_PER_EL = 6;

bool TAdriaMsg::IsComplete() const {
	if (!IsLastReadEol) { return false; }
	if (!HasMethod() || !HasCommand()) { return false; }

	if (IsPush() || IsPost()) {
		if (Length < 0) { return false; }

		int RealLen = Content.Len();
		if (RealLen != Length) { return false; }
	}

	return true;
}

void TAdriaMsg::ReadUntil(const PSIn& In, const TStr& EndStr, TChA& Out) const {
	const int BuffLen = EndStr.Len();

	const char* Target = EndStr.CStr();
	char* Buff = new char[BuffLen];

	while (!In->Eof()) {
		char Ch = In->GetCh();

		Out += Ch;

		// shift buffer
		for (int i = 0; i < BuffLen-1; i++) { Buff[i] = Buff[i+1]; }
		Buff[BuffLen - 1] = Ch;

		// check if the delimiter was reached
		if (TAdriaMsg::BuffsEq(Buff, Target, BuffLen))
			break;
	}

	delete Buff;
}

void TAdriaMsg::ReadLine(const PSIn& In, TChA& Out) const {
	ReadUntil(In, "\r\n", Out);
}

void TAdriaMsg::Read(const PSIn& SIn) {
	// read the method
	TChA LineBuff;
	ReadLine(SIn, LineBuff);

	// ignore the EOL
	LineBuff.DelLastCh();	LineBuff.DelLastCh();

	if (LineBuff.IsPrefix(TAdriaMsg::PUSH)) {
		Method = TAdriaMsgMethod::ammPush;
	} else if (LineBuff.IsPrefix(TAdriaMsg::POST)) {
		Method = TAdriaMsgMethod::ammPost;
	} else if (LineBuff.IsPrefix(TAdriaMsg::GET)) {
		Method = TAdriaMsgMethod::ammGet;
	} else {
		throw TExcept::New(TStr("Invalid protocol method: ") + LineBuff, "TAdriaMsg::Read(const PSIn& SIn)");
	}

	// parse the command
	int SpaceIdx = LineBuff.SearchCh(' ', 3);
	// check if the line has parameters
	int QuestionMrkIdx = LineBuff.SearchCh('?', SpaceIdx+1);
	int AndIdx = LineBuff.SearchCh('&', SpaceIdx+1);

	// multiple cases
	if (QuestionMrkIdx < 0 && AndIdx < 0) {
		// only the command is present
		Command = LineBuff.GetSubStr(SpaceIdx+1, LineBuff.Len());
	} else if (AndIdx < 0) {
		// only the parameters are present
		Command = LineBuff.GetSubStr(SpaceIdx+1, QuestionMrkIdx-1);
		Params = LineBuff.GetSubStr(QuestionMrkIdx+1, LineBuff.Len());
	} else if (QuestionMrkIdx < 0) {
		// only the ID is present
		Command = LineBuff.GetSubStr(SpaceIdx+1, AndIdx-1);
		ComponentId = LineBuff.GetSubStr(AndIdx+1, LineBuff.Len());
	} else {
		// both the parameters and the ID are present
		Command = LineBuff.GetSubStr(SpaceIdx+1, QuestionMrkIdx-1);
		Params = LineBuff.GetSubStr(QuestionMrkIdx+1, AndIdx-1);
		ComponentId = LineBuff.GetSubStr(AndIdx+1, LineBuff.Len());
	}

	if (HasContent()) {
		// parse the length
		LineBuff.Clr();
		ReadLine(SIn, LineBuff);
		// ignore the EOL
		LineBuff.DelLastCh();	LineBuff.DelLastCh();

		TStr LenStr = LineBuff.GetSubStr(7, LineBuff.Len());
		Length = LenStr.GetInt();

		// parse the content
		for (int i = 0; i < Length; i++) {
			Content += SIn->GetCh();
		}

		// newline at the end
		SIn->GetCh();	SIn->GetCh();
	}
}

TStr TAdriaMsg::GetStr() const {
	TChA Res;

	switch (Method) {
	case TAdriaMsgMethod::ammPush:
		Res += TAdriaMsg::PUSH;
		break;
	case TAdriaMsgMethod::ammPost:
		Res += TAdriaMsg::POST;
		break;
	case TAdriaMsgMethod::ammGet:
		Res += TAdriaMsg::GET;
		break;
	default:
		throw TExcept::New("Invalid method!", "TAdriaMsg::GetStr()");
	}

	Res.Push(' ');
	Res += Command;

	if (HasParams()) { Res.Push('?'); Res += Params; }

	Res += "\r\n";

	if (HasContent()) {
		Res += TStr("Length=") + TInt::GetStr(Length) + "\r\n";
		Res += Content + TStr("\r\n");
	}

	return Res;
}

bool TAdriaMsg::BuffsEq(const char* Buff1, const char* Buff2, const int& BuffLen) {
	for (int i = 0; i < BuffLen; i++) {
		if (Buff1[i] != Buff2[i])
			return false;
	}
	return true;
}
