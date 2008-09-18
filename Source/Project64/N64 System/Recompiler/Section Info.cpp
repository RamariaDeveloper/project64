#include "..\..\N64 System.h"
#pragma warning(disable:4355) // Disable 'this' : used in base member initializer list

CBlockInfo::CBlockInfo(DWORD VAddr, BYTE * RecompPos) :
	StartVAddr(VAddr), 
	EndVAddr(VAddr),
	CompiledLocation(RecompPos),
	NoOfSections(1),
	ParentSection(this, VAddr,1)
{
	ParentSection.AddParent(NULL);
	ParentSection.LinkAllowed = false;
}

CJumpInfo::CJumpInfo()
{
	TargetPC      = (DWORD)-1;
	BranchLabel   = "";
	LinkLocation  = NULL;
	LinkLocation2 = NULL;
	FallThrough   = false;
	PermLoop      = false;
	DoneDelaySlot = false;
}

CBlockSection::CBlockSection( CBlockInfo * _BlockInfo, DWORD StartAddr, DWORD ID) :
	BlockInfo(_BlockInfo)
{
	JumpSection        = NULL;
	ContinueSection    = NULL;
	CompiledLocation   = NULL;

	SectionID          = ID;
	Test               = 0;
	Test2              = 0;
	InLoop             = false;
	LinkAllowed        = true;
	DelaySlotSection   = false;

	StartPC            = StartAddr;
	CompilePC          = StartAddr;
}

bool CBlockSection::IsAllParentLoops(CBlockSection * Parent, bool IgnoreIfCompiled, DWORD Test) 
{ 
	if (IgnoreIfCompiled && Parent->CompiledLocation != NULL) { return true; }
	if (!InLoop) { return false; }
	if (!Parent->InLoop) { return false; }
	if (Parent->ParentSection.empty()) { return false; }
	if (this == Parent) { return true; }	
	if (Parent->Test == Test) { return true; }
	Parent->Test = Test;
		
	for (SECTION_LIST::iterator iter = Parent->ParentSection.begin(); iter != Parent->ParentSection.end(); iter++)
	{
		CBlockSection * ParentSection = *iter;
		if (!IsAllParentLoops(ParentSection,IgnoreIfCompiled,Test)) { return false; }
	}
	return true;
}

void CBlockSection::UnlinkParent( CBlockSection * Parent, bool AllowDelete, bool ContinueSection )
{
	if (this == NULL) 
	{
		return;
	}
	
	SECTION_LIST::iterator iter = ParentSection.begin();
	while ( iter != ParentSection.end())
	{
		CBlockSection * ParentIter = *iter;
		if (ParentIter == Parent && (Parent->ContinueSection != this || Parent->JumpSection != this))
		{
			ParentSection.erase(iter);
			iter = ParentSection.begin();
		} else {
			iter++;
		}
	}

//	if (Parent->ContinueSection != Parent->JumpSection)
//	{
//		if (!ContinueSection && Parent->ContinueSection == this)
//		{
//			Notify().BreakPoint(__FILE__,__LINE__);
//		}
//	}
	if (ContinueSection && Parent->ContinueSection == this)
	{
		Parent->ContinueSection = NULL;
	}
//	if (Parent->ContinueSection != Parent->JumpSection)
//	{
//		if (ContinueSection && Parent->JumpSection == this)
//		{
//			Notify().BreakPoint(__FILE__,__LINE__);
//		}
//	}
	if (!ContinueSection && Parent->JumpSection == this)
	{
		Parent->JumpSection = NULL;
	}
	if (AllowDelete)
	{
		bool KillMe = true;
		for (SECTION_LIST::iterator iter = ParentSection.begin(); iter != ParentSection.end(); iter++)
		{
			if (!IsAllParentLoops(*iter,false,GetNewTestValue()))
			{
				KillMe = false;
				break;
			}
		}
		if (KillMe)
		{
			delete this;
		}
	}	
}

CBlockSection::~CBlockSection ( void )
{
	while (ParentSection.size() > 0)
	{
		CBlockSection * Parent = *ParentSection.begin();
		if (Parent->ContinueSection == this) { UnlinkParent(Parent, false, true); }
		if (Parent->JumpSection == this)     { UnlinkParent(Parent, false, false); }
	}
	
	if (ContinueSection)
	{
		ContinueSection->UnlinkParent(this, true, true);
		if (ContinueSection)
		{
			Notify().BreakPoint(__FILE__,__LINE__);
		}
		ContinueSection = NULL;
	}
	if (JumpSection)
	{
		JumpSection->UnlinkParent(this, true, false);
		if (JumpSection)
		{
			Notify().BreakPoint(__FILE__,__LINE__);
		}
		JumpSection = NULL;
	}
}

DWORD CBlockSection::GetNewTestValue(void) 
{
	static DWORD LastTest = 0;
	if (LastTest == 0xFFFFFFFF) { LastTest = 0; }
	LastTest += 1;
	return LastTest;
}

void CBlockSection::TestRegConstantStates( CRegInfo & Base, CRegInfo & Reg  )
{
	for (int count = 0; count < 32; count++) {
		if (Reg.MipsRegState(count) != Base.MipsRegState(count)) {
			Reg.MipsRegState(count) = CRegInfo::STATE_UNKNOWN;
		}
		if (Reg.IsConst(count))
		{
			if (Reg.Is32Bit(count))
			{
				if (Reg.MipsRegLo(count) != Base.MipsRegLo(count)) {
					Reg.MipsRegState(count) = CRegInfo::STATE_UNKNOWN;
				}
			} else {
				if (Reg.MipsReg(count) != Base.MipsReg(count)) {
					Reg.MipsRegState(count) = CRegInfo::STATE_UNKNOWN;
				}
			}

		}
	}
}

void CBlockSection::AddParent(CBlockSection * Parent )
{
	if (this == NULL) { return; }
	if (Parent == NULL) 
	{
		RegStart.Initilize();
		RegWorking = RegStart;
		return;
	}

	// check to see if we already have the parent in the list
	for (SECTION_LIST::iterator iter = ParentSection.begin(); iter != ParentSection.end(); iter++)
	{
		if (*iter == Parent)
		{
			return;
		}
	}
	ParentSection.push_back(Parent);

	if (ParentSection.size() == 1)
	{
		if (Parent->ContinueSection == this) {
			RegStart = Parent->Cont.RegSet;
		} else if (Parent->JumpSection == this) {
			RegStart = Parent->Jump.RegSet;
		} else {
			Notify().DisplayError("How are these sections joined?????");
		}
		RegWorking = RegStart;
	} else {
		if (Parent->ContinueSection == this) {
			TestRegConstantStates(Parent->Cont.RegSet,RegStart);
		}
		if (Parent->JumpSection == this) {
			TestRegConstantStates(Parent->Jump.RegSet,RegStart);
		}
		RegWorking = RegStart;
	}
}

void CBlockSection::ResetX86Protection (void)
{
	for (int count = 1; count < 10; count ++) 
	{ 
		RegWorking.x86Protected(count) = false;
	}
}

void CRegInfo::Initilize ( void )
{
	int count;
	
	MIPS_RegState[0]  = STATE_CONST_32;
	MIPS_RegVal[0].DW = 0;
	for (count = 1; count < 32; count ++ ) {
		MIPS_RegState[count]   = STATE_UNKNOWN;
		MIPS_RegVal[count].DW = 0;

	}
	for (count = 0; count < 10; count ++ ) {
		x86reg_MappedTo[count]  = NotMapped;
		x86reg_Protected[count] = false;
		x86reg_MapOrder[count]  = 0;
	}
	CycleCount = 0;
	RandomModifier = 0;

	Stack_TopPos = 0;
	for (count = 0; count < 8; count ++ ) {
		x86fpu_MappedTo[count] = -1;
		x86fpu_State[count] = FPU_Unkown;
		x86fpu_RoundingModel[count] = RoundDefault;
	}
	Fpu_Used = false;
	RoundingModel = RoundUnknown;
}

CRegInfo::REG_STATE CRegInfo::ConstantsType (__int64 Value) 
{
	if (((Value >> 32) == -1) && ((Value & 0x80000000) != 0)) { return STATE_CONST_32; } 
	if (((Value >> 32) == 0) && ((Value & 0x80000000) == 0)) { return STATE_CONST_32; } 
	return STATE_CONST_64;
}

bool CRegInfo::compare(const CRegInfo& right) const
{
	int count;

	for (count = 0; count < 32; count ++ ) {
		if (MIPS_RegState[count] != right.MIPS_RegState[count]) 
		{
			return false; 
		}
		if (MIPS_RegState[count] == STATE_UNKNOWN)
		{
			continue;
		}
		if (MIPS_RegVal[count].DW != right.MIPS_RegVal[count].DW) 
		{
			return false; 
		}
	}
	for (count = 0; count < 10; count ++ ) {
		if (x86reg_MappedTo[count] != right.x86reg_MappedTo[count]) { return false; }
		if (x86reg_Protected[count] != right.x86reg_Protected[count]) { return false; }
		if (x86reg_MapOrder[count]  != right.x86reg_MapOrder[count]) { return false; }
	}
	if (CycleCount != right.CycleCount) { return false; }
	if (RandomModifier != right.RandomModifier) { return false; }
	if (Stack_TopPos != right.Stack_TopPos) { return false; }

	for (count = 0; count < 8; count ++ ) {
		if (x86fpu_MappedTo[count]  != right.x86fpu_MappedTo[count]) { return false; }
		if (x86fpu_State[count]  != right.x86fpu_State[count]) { return false; }
		if (x86fpu_RoundingModel[count]  != right.x86fpu_RoundingModel[count]) { return false; }
	}
	if (Fpu_Used != right.Fpu_Used) { return false; }
	if (RoundingModel != right.RoundingModel) { return false; }
	return true;
}

bool CRegInfo::operator!=(const CRegInfo& right) const
{
	return !compare(right);
}
