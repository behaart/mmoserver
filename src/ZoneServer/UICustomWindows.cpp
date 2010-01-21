/*
---------------------------------------------------------------------------------------
This source file is part of swgANH (Star Wars Galaxies - A New Hope - Server Emulator)
For more information, see http://www.swganh.org


Copyright (c) 2006 - 2010 The swgANH Team

---------------------------------------------------------------------------------------
*/
#include "WorldConfig.h"
#include "UIManager.h"
#include "Common/atMacroString.h"
#include "PlayerObject.h"
#include "UIInputBox.h"
#include "UIListBox.h"
#include "UIMessageBox.h"
#include "UIOpcodes.h"
#include "LogManager/LogManager.h"

//#include "Common/DispatchClient.h"
//#include "Common/Message.h"
//#include "Common/MessageDispatch.h"
//#include "Common/MessageFactory.h"

//======================================================================================================================


//======================================================================================================================
void UIManager::createNewDiagnoseListBox(UICallback* callback,PlayerObject* Medic, PlayerObject* Patient)
{
	BStringVector attributesMenu;

	BString FirstName = Patient->getFirstName(); FirstName.toUpper();
	BString LastName = Patient->getLastName(); LastName.toUpper();

	int8 title[64];
	sprintf(title,"PATIENT %s %s'S WOUNDS",FirstName.getAnsi(), LastName.getAnsi());

	int8 desc[512];
	sprintf(desc, "Below is a listing of the Wound and Battle Fatigue levels of %s %s. Wounds are healed through /tendWound or use of Wound Medpacks. High levels of Battle Fatigue can inhibit the healing process, and Battle Fatigue can only be healed by the patient choosing to watch performing entertainers",Patient->getFirstName().getAnsi(), Patient->getLastName().getAnsi());

	int8 Health[32];
	sprintf(Health,"Health -- %i",Patient->getHam()->mHealth.getWounds());
	attributesMenu.push_back(Health);

	int8 Strength[32];
	sprintf(Strength,"Strength -- %i",Patient->getHam()->mStrength.getWounds());
	attributesMenu.push_back(Strength);
	
	int8 Constitution[32];
	sprintf(Constitution,"Constitution -- %i",Patient->getHam()->mConstitution.getWounds());
	attributesMenu.push_back(Constitution);
	
	int8 Action[32];
	sprintf(Action,"Action -- %i",Patient->getHam()->mAction.getWounds());
	attributesMenu.push_back(Action);

	int8 Quickness[32];
	sprintf(Quickness,"Quickness -- %i",Patient->getHam()->mQuickness.getWounds());
	attributesMenu.push_back(Quickness);

	int8 Stamina[32];
	sprintf(Stamina,"Stamina -- %i",Patient->getHam()->mStamina.getWounds());
	attributesMenu.push_back(Stamina);

	int8 Mind[32];
	sprintf(Mind,"Mind -- %i",Patient->getHam()->mMind.getWounds());
	attributesMenu.push_back(Mind);

	int8 Focus[32];
	sprintf(Focus,"Focus -- %i",Patient->getHam()->mFocus.getWounds());
	attributesMenu.push_back(Focus);

	int8 Willpower[32];
	sprintf(Willpower,"Willpower -- %i",Patient->getHam()->mWillpower.getWounds());
	attributesMenu.push_back(Willpower);

	int8 BattleFatigue[32];
	sprintf(BattleFatigue,"Battle Fatigue -- %i",Patient->getHam()->getBattleFatigue());
	attributesMenu.push_back(BattleFatigue);

	createNewListBox(callback,"handleDiagnoseMenu",title, desc, attributesMenu, Medic, SUI_Window_ListBox,SUI_LB_OK,Patient->getId());
}
