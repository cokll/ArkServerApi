#include "Main.h"
#include "..\Public\SafeZone.h"
#include "SZManager.h"
#pragma comment(lib, "ArkApi.lib")


#pragma region Config

	#pragma region Init
		FString ServerName;
		void InitConfig()
		{
			Log::GetLog()->info("Loading SafeZones.");
			std::ifstream file(ArkApi::Tools::GetCurrentDir() + "/ArkApi/Plugins/SafeZone/config.json");
			if (!file.is_open())
			{
				Log::GetLog()->info("Could not open file config.json");
				throw;
			}
			file >> SafeZoneConfig;
			file.close();

			NotificationCheckSeconds = SafeZoneConfig["SafeZones"]["SafeZoneNotificationCheckSeconds"];
			if (NotificationCheckSeconds < 2) NotificationCheckSeconds = 2;
			else NotificationCheckSeconds = NotificationCheckSeconds - 1;

			nlohmann::json Messages;
			FString Msgs[6];
			int j = 0;
			std::string Data;

			Data = SafeZoneConfig["SafeZones"]["ServerName"];
			ServerName = FString(ArkApi::Tools::ConvertToWideStr(Data).c_str());
			Data = SafeZoneConfig["SafeZones"]["ClaimItemsCommand"];
			ClaimItemsCommand = ArkApi::Tools::ConvertToWideStr(Data);

			auto SafeZonesDistanceMap = SafeZoneConfig["SafeZones"]["DistanceSafeZones"];
			for (const auto& safeZone : SafeZonesDistanceMap)
			{
				auto Pos = safeZone["Position"];

				Messages = safeZone["Messages"];
				j = 0;
				for (nlohmann::json::iterator it = Messages.begin(); it != Messages.end(); it++)
				{
					Data = *it;
					Msgs[j++] = FString(ArkApi::Tools::ConvertToWideStr(Data).c_str());
					if (j == 6) break;
				}

				SafeZoneManager::ItemArray Items;
				auto SafeZonesItemMap = safeZone["Items"];
				for (const auto& szitem : SafeZonesItemMap)
				{
					Data = szitem["Blueprint"];
					Items.push_back(SafeZoneManager::ItemS(FString(Data.c_str()), szitem["Quantity"], szitem["Quality"], szitem["IsBlueprint"]));
				}

				Data = safeZone["Name"];

				auto ServRGB1 = safeZone["EnterNotificationColour"], ServRGB2 = safeZone["LeaveNotificationColour"];
				SafeZoneManager::SZManager::Get().SafeZoneDistanceMap.push_back(SafeZoneManager::SafeZoneDistanceS(FString(ArkApi::Tools::ConvertToWideStr(Data).c_str()), FVector(Pos[0], Pos[1], Pos[2]), safeZone["PreventPVP"], safeZone["PreventStructureDamage"], safeZone["PreventBuilding"], safeZone["Distance"], Msgs, Items, FLinearColor(ServRGB1[0], ServRGB1[1], ServRGB1[2], ServRGB1[3]), FLinearColor(ServRGB2[0], ServRGB2[1], ServRGB2[2], ServRGB2[3])));

				Log::GetLog()->info("SafeZone: {} ({}, {}, {}) Added", Data.c_str(), (float)Pos[0], (float)Pos[1], (float)Pos[2]);
			}
		}
	#pragma endregion

#pragma endregion

#pragma region Hooks

	#pragma region Init
		DECLARE_HOOK(APrimalStructure_IsAllowedToBuild, int, APrimalStructure*, APlayerController*, FVector, FRotator, FPlacementData*, bool, FRotator, bool);
		DECLARE_HOOK(APrimalCharacter_TakeDamage, float, APrimalCharacter*, float, FDamageEvent*, AController*, AActor*);
		DECLARE_HOOK(APrimalStructure_TakeDamage, float, APrimalStructure*, float, FDamageEvent*, AController*, AActor*);
		DECLARE_HOOK(AShooterGameMode_Logout, void, AShooterGameMode*, AController*);
		DECLARE_HOOK(AShooterPlayerController_TickActor, void, AShooterPlayerController*);

		SafeZoneManager::SafeZonePlayerDataItr SafeZoneItr;
		int SecCounter = 0;
		void OnTimer();

		void InitHooks()
		{
			ArkApi::GetHooks().SetHook("APrimalStructure.IsAllowedToBuild", &Hook_APrimalStructure_IsAllowedToBuild, reinterpret_cast<LPVOID*>(&APrimalStructure_IsAllowedToBuild_original));
			ArkApi::GetHooks().SetHook("APrimalCharacter.TakeDamage", &Hook_APrimalCharacter_TakeDamage, reinterpret_cast<LPVOID*>(&APrimalCharacter_TakeDamage_original));
			ArkApi::GetHooks().SetHook("APrimalStructure.TakeDamage", &Hook_APrimalStructure_TakeDamage, reinterpret_cast<LPVOID*>(&APrimalStructure_TakeDamage_original));
			ArkApi::GetHooks().SetHook("AShooterGameMode.Logout", &Hook_AShooterGameMode_Logout, reinterpret_cast<LPVOID*>(&AShooterGameMode_Logout_original));
			const bool bSafeZoneNotifications = SafeZoneConfig["SafeZones"]["SafeZoneNotifications"];
			if (bSafeZoneNotifications)
			{
				const bool bInstantNotifications = SafeZoneConfig["SafeZones"]["InstantNotifications"];
				if(bInstantNotifications) ArkApi::GetHooks().SetHook("AShooterPlayerController.TickActor", &Hook_AShooterPlayerController_TickActor, reinterpret_cast<LPVOID*>(&AShooterPlayerController_TickActor_original));
				else ArkApi::GetCommands().AddOnTimerCallback("OnSafeZoneTimer", &OnTimer);
			}
		}
	#pragma endregion

	#pragma region Destory
		void RemoveHooks()
		{
			ArkApi::GetHooks().DisableHook("APrimalStructure.IsAllowedToBuild", &Hook_APrimalStructure_IsAllowedToBuild);
			ArkApi::GetHooks().DisableHook("APrimalCharacter.TakeDamage", &Hook_APrimalCharacter_TakeDamage);
			ArkApi::GetHooks().DisableHook("APrimalStructure.TakeDamage", &Hook_APrimalStructure_TakeDamage);
			ArkApi::GetHooks().DisableHook("AShooterGameMode.Logout", &Hook_AShooterGameMode_Logout);
			const bool bSafeZoneNotifications = SafeZoneConfig["SafeZones"]["SafeZoneNotifications"];
			if (bSafeZoneNotifications)
			{
				const bool bInstantNotifications = SafeZoneConfig["SafeZones"]["InstantNotifications"];
				if (bInstantNotifications) ArkApi::GetHooks().DisableHook("AShooterPlayerController.TickActor", &Hook_AShooterPlayerController_TickActor);
				else ArkApi::GetCommands().RemoveOnTimerCallback("OnSafeZoneTimer");
			}
		}
	#pragma endregion

	#pragma region Functions
		bool CheckSafeZone(const FVector& Position, AShooterPlayerController* ASPC, const int Type)
		{
			const bool bAdminIgnoreChecks = SafeZoneConfig["SafeZones"]["AdminIgnoreChecks"];
			if (bAdminIgnoreChecks && ASPC && ASPC->GetPlayerCharacter() && ASPC->GetPlayerCharacter()->bIsServerAdminField()()) return false;
			SafeZoneManager::SafeZoneDistanceSItr it = std::find_if(SafeZoneManager::SZManager::Get().SafeZoneDistanceMap.begin(), SafeZoneManager::SZManager::Get().SafeZoneDistanceMap.end(), [Position](SafeZoneManager::SafeZoneDistanceS& SZ) -> bool { return SZ.IsInArea(Position); });
			if (it != SafeZoneManager::SZManager::Get().SafeZoneDistanceMap.end())
			{
				bool IsProtected = true;
				switch (Type)
				{
				case 0:
					IsProtected = it->isBuilding;
					break;
				case 1:
					IsProtected = it->isPVP;
					break;
				case 2:
					IsProtected = it->PreventStructureDamage;
					break;
				}
				if (Type != -1 && ASPC != nullptr && it->Messages[Type].Len() != 0)ArkApi::GetApiUtils().SendChatMessage(ASPC, ServerName, *it->Messages[Type]);
				return IsProtected;
			}
			return false;
		}

		void DisplaySafeZoneNotifications(AShooterPlayerController* Player)
		{
			if ((SafeZoneItr = SafeZoneManager::SZManager::Get().FindPlayer(Player->LinkedPlayerIDField()())) != SafeZoneManager::SZManager::Get().end())
			{
				const FVector Position = Player->DefaultActorLocationField()();
				SafeZoneManager::SafeZoneDistanceSItr it = SafeZoneManager::SZManager::Get().FindSafeZone(Position);
				if (it != SafeZoneManager::SZManager::Get().SafeZoneDistanceMap.end())
				{
					if (!SafeZoneItr->IsSafeArea())
					{
						const int iDisplayType = SafeZoneConfig["SafeZones"]["DisplayType"];
						switch (iDisplayType)
						{
							case 0:
							{
								const float fScale = SafeZoneConfig["SafeZones"]["NotificationScale"], fDisplayTime = SafeZoneConfig["SafeZones"]["NotificationShowDelay"];
								ArkApi::GetApiUtils().SendNotification(Player, it->EnterNotificationColour, fScale, fDisplayTime, nullptr, *it->Messages[3], *it->Name);
								ArkApi::GetApiUtils().SendChatMessage(Player, ServerName, fmt::format(L"<RichColor Color=\"{}, {}, {}, {}\">{}</>", it->EnterNotificationColour.R, it->EnterNotificationColour.G, it->EnterNotificationColour.B, it->EnterNotificationColour.A, *it->Messages[3]).c_str(), *it->Name);
							} break;
							case 1:
							{
								const float fScale = SafeZoneConfig["SafeZones"]["NotificationScale"], fDisplayTime = SafeZoneConfig["SafeZones"]["NotificationShowDelay"];
								ArkApi::GetApiUtils().SendNotification(Player, it->EnterNotificationColour, fScale, fDisplayTime, nullptr, *it->Messages[3], *it->Name);
							} break;
							case 2:
							{
								ArkApi::GetApiUtils().SendChatMessage(Player, ServerName, fmt::format(L"<RichColor Color=\"{}, {}, {}, {}\">{}</>", it->EnterNotificationColour.R, it->EnterNotificationColour.G, it->EnterNotificationColour.B, it->EnterNotificationColour.A, *it->Messages[3]).c_str(), *it->Name);
							} break;
						}
						SafeZoneItr->SetSafeArea(true, it->Name, it->Messages[4], it->LeaveNotificationColour);
						SafeZoneManager::SZManager::Get().ExecuteCallBacks(Player, true);
					}
				}
				else if (SafeZoneItr->IsSafeArea())
				{
					if (SafeZoneItr->LeaveMessage.Len() != 0)
					{
						const int iDisplayType = SafeZoneConfig["SafeZones"]["DisplayType"];
						switch (iDisplayType)
						{
							case 0:
							{
								const float fScale = SafeZoneConfig["SafeZones"]["NotificationScale"], fDisplayTime = SafeZoneConfig["SafeZones"]["NotificationShowDelay"];
								ArkApi::GetApiUtils().SendNotification(Player, SafeZoneItr->LeaveNotificationColour, fScale, fDisplayTime, nullptr, *SafeZoneItr->LeaveMessage, *SafeZoneItr->Name);
								ArkApi::GetApiUtils().SendChatMessage(Player, ServerName, fmt::format(L"<RichColor Color=\"{}, {}, {}, {}\">{}</>", SafeZoneItr->LeaveNotificationColour.R, SafeZoneItr->LeaveNotificationColour.G, SafeZoneItr->LeaveNotificationColour.B, SafeZoneItr->LeaveNotificationColour.A, *SafeZoneItr->LeaveMessage).c_str(), *SafeZoneItr->Name);
							} break;
							case 1:
							{
								const float fScale = SafeZoneConfig["SafeZones"]["NotificationScale"], fDisplayTime = SafeZoneConfig["SafeZones"]["NotificationShowDelay"];
								ArkApi::GetApiUtils().SendNotification(Player, SafeZoneItr->LeaveNotificationColour, fScale, fDisplayTime, nullptr, *SafeZoneItr->LeaveMessage, *SafeZoneItr->Name);
							} break;
							case 2:
							{
								ArkApi::GetApiUtils().SendChatMessage(Player, ServerName, fmt::format(L"<RichColor Color=\"{}, {}, {}, {}\">{}</>", SafeZoneItr->LeaveNotificationColour.R, SafeZoneItr->LeaveNotificationColour.G, SafeZoneItr->LeaveNotificationColour.B, SafeZoneItr->LeaveNotificationColour.A, *SafeZoneItr->LeaveMessage).c_str(), *SafeZoneItr->Name);
							} break;
						}
					}
					SafeZoneItr->SetSafeArea(false);
					SafeZoneManager::SZManager::Get().ExecuteCallBacks(Player, false);
				}
			}
		}

		bool APrimalStructureIsAllowedBuildCheckSafeZone(APlayerController* PC, FVector AtLocation)
		{
			if (PC && PC->IsA(AShooterPlayerController::GetPrivateStaticClass()))
			{
				AShooterPlayerController* ASPC = static_cast<AShooterPlayerController*>(PC);
				if (ASPC && CheckSafeZone(AtLocation, ASPC, 0))  return true;
			}
			return false;
		}

		long long GetPlayerID(APrimalCharacter* _this)
		{
			AShooterCharacter* shooterCharacter = static_cast<AShooterCharacter*>(_this);
			return (shooterCharacter && shooterCharacter->GetPlayerData()) ? shooterCharacter->GetPlayerData()->MyDataField()()->PlayerDataIDField()() : -1;
		}

		bool PrimalCharacterDamageCheckSafeZone(APrimalCharacter* Victim, AController* Attacker, AActor* DamageCauser)
		{
			if (Victim && Victim->IsA(AShooterCharacter::GetPrivateStaticClass()))
			{
				if (Attacker && !Attacker->IsLocalController() && Attacker->IsA(AShooterPlayerController::StaticClass()))
				{
					AShooterPlayerController* AttackerShooterController = static_cast<AShooterPlayerController*>(Attacker);
					if (AttackerShooterController && AttackerShooterController->LinkedPlayerIDField()() != GetPlayerID(Victim) && CheckSafeZone(Victim->RootComponentField()()->RelativeLocationField()(), AttackerShooterController, 1)) return true;
				}
				else
				{
					const bool bSafeZoneKillAgressiveDino = SafeZoneConfig["SafeZones"]["SafeZoneKillAgressiveDino"];
					if (bSafeZoneKillAgressiveDino && CheckSafeZone(Victim->RootComponentField()()->RelativeLocationField()(), nullptr, 1) && Attacker && Attacker->IsLocalController() && DamageCauser) DamageCauser->Destroy(true, false);
					return true;
				}
			}
			return false;
		}

		bool PrimalStructureDamageCheckSafeZone(APrimalStructure* structure, AController* Attacker, AActor* DamageCauser)
		{
			if (structure)
			{
				FVector Pos = structure->RootComponentField()()->RelativeLocationField()();
				if (Attacker && !Attacker->IsLocalController() && Attacker->IsA(AShooterPlayerController::StaticClass()))
				{
					AShooterPlayerController* AttackerShooterController = static_cast<AShooterPlayerController*>(Attacker);
					if (AttackerShooterController && CheckSafeZone(structure->RootComponentField()()->RelativeLocationField()(), AttackerShooterController, 2)) return true;
				}
				else
				{
					const bool bSafeZoneKillAgressiveDino = SafeZoneConfig["SafeZones"]["SafeZoneKillAgressiveDino"];
					if (bSafeZoneKillAgressiveDino && CheckSafeZone(structure->RootComponentField()()->RelativeLocationField()(), nullptr, 2) && Attacker && Attacker->IsLocalController() && DamageCauser) DamageCauser->Destroy(true, false);
					return true;
				}
			}
			return false;
		}
	#pragma endregion

	#pragma region CallBacks
		void OnTimer()
		{
			if (SecCounter++ == NotificationCheckSeconds)
			{
				if (!ArkApi::GetApiUtils().GetWorld())
				{
					SecCounter = 0;
					return;
				}
				const auto& player_controllers = ArkApi::GetApiUtils().GetWorld()->PlayerControllerListField()();
				for (TWeakObjectPtr<APlayerController> PlayerCon : player_controllers)
				{
					AShooterPlayerController* Player = static_cast<AShooterPlayerController*>(PlayerCon.Get());
					if (Player && Player->GetPlayerCharacter() && !Player->GetPlayerCharacter()->IsDead() && Player->LinkedPlayerIDField()() != 0) DisplaySafeZoneNotifications(Player);
				}
				SecCounter = 0;
			}
		}
	#pragma endregion

	#pragma region Hooks
		int _cdecl Hook_APrimalStructure_IsAllowedToBuild(APrimalStructure* _this, APlayerController* PC, FVector AtLocation, FRotator AtRotation, FPlacementData * OutPlacementData, bool bDontAdjustForMaxRange, FRotator PlayerViewRotation, bool bFinalPlacement)
		{
			return ((bFinalPlacement && APrimalStructureIsAllowedBuildCheckSafeZone(PC, AtLocation)) ? 0 : APrimalStructure_IsAllowedToBuild_original(_this, PC, AtLocation, AtRotation, OutPlacementData, bDontAdjustForMaxRange, PlayerViewRotation, bFinalPlacement));
		}

		float _cdecl Hook_APrimalCharacter_TakeDamage(APrimalCharacter* _this, float Damage, FDamageEvent* DamageEvent, AController* EventInstigator, AActor* DamageCauser)
		{
			return (PrimalCharacterDamageCheckSafeZone(_this, EventInstigator, DamageCauser) ? 0 : APrimalCharacter_TakeDamage_original(_this, Damage, DamageEvent, EventInstigator, DamageCauser));
		}

		float _cdecl Hook_APrimalStructure_TakeDamage(APrimalStructure* _this, float Damage, FDamageEvent* DamageEvent, AController* EventInstigator, AActor* DamageCauser)
		{
			return (PrimalStructureDamageCheckSafeZone(_this, EventInstigator, DamageCauser) ? 0 : APrimalStructure_TakeDamage_original(_this, Damage, DamageEvent, EventInstigator, DamageCauser));
		}

		void Hook_AShooterPlayerController_TickActor(AShooterPlayerController* Player)
		{
			if (Player && Player->GetPlayerCharacter() && !Player->GetPlayerCharacter()->IsDead() && Player->LinkedPlayerIDField()() != 0) DisplaySafeZoneNotifications(Player);
			AShooterPlayerController_TickActor_original(Player);
		}

		void _cdecl Hook_AShooterGameMode_Logout(AShooterGameMode* _this, AController* Exiting)
		{
			if (Exiting && Exiting->IsA(AShooterPlayerController::GetPrivateStaticClass()))
			{
				AShooterPlayerController* Player = static_cast<AShooterPlayerController*>(Exiting);
				if (Player && Player->LinkedPlayerIDField()() != 0) SafeZoneManager::SZManager::Get().RemovePlayer(Player->LinkedPlayerIDField()());
			}
			AShooterGameMode_Logout_original(_this, Exiting);
		}
	#pragma endregion

#pragma endregion

#pragma region Commands

	#pragma region Commands
		FVector szSetPos = FVector(0, 0, 0);
		void SetPos(AShooterPlayerController* player, FString* message, int mode)
		{
			if (!player || !player->PlayerStateField()() || !player->GetPlayerCharacter() || player->GetPlayerCharacter()->IsDead()) return;
			if (!player->GetPlayerCharacter()->bIsServerAdminField()())
			{
				ArkApi::GetApiUtils().SendServerMessage(player, FLinearColor(255, 0, 0), "Please login as admin to use this command");
				return;
			}
			szSetPos = player->DefaultActorLocationField()();
			ArkApi::GetApiUtils().SendServerMessage(player, FLinearColor(0, 255, 0), "Pos: {0:.0f}, {1:.0f}, {2:.0f}", szSetPos.X, szSetPos.Y, szSetPos.Z);
			Log::GetLog()->info("Position: {}, {}, {}", szSetPos.X, szSetPos.Y, szSetPos.Z);
		}

		void Dist(AShooterPlayerController* player, FString* message, int mode)
		{
			if (!player || !player->PlayerStateField()() || !player->GetPlayerCharacter() || player->GetPlayerCharacter()->IsDead()) return;
			if (!player->GetPlayerCharacter()->bIsServerAdminField()())
			{
				ArkApi::GetApiUtils().SendServerMessage(player, FLinearColor(255, 0, 0), "Please login as admin to use this command");
				return;
			}
			if (szSetPos.X == 0 && szSetPos.Y == 0 && szSetPos.Z == 0)
			{
				ArkApi::GetApiUtils().SendServerMessage(player, FLinearColor(255, 0, 0), "Please use /szsetpos first");
				return;
			}
			ArkApi::GetApiUtils().SendServerMessage(player, FLinearColor(0, 255, 0), "Distance: {}", (int)FVector::Distance(player->DefaultActorLocationField()(), szSetPos));
			Log::GetLog()->info("Distance: {}", (int)FVector::Distance(player->DefaultActorLocationField()(), szSetPos));
		}

		void TPCoord(AShooterPlayerController* player, FString* message, int mode)
		{
			if (!player || !player->PlayerStateField()() || !player->GetPlayerCharacter() || player->GetPlayerCharacter()->IsDead()) return;
			if (!player->GetPlayerCharacter()->bIsServerAdminField()())
			{
				ArkApi::GetApiUtils().SendServerMessage(player, FLinearColor(255, 0, 0), "Please login as admin to use this command");
				return;
			}
			TArray<FString> Parsed;
			message->ParseIntoArray(Parsed, L" ", true);
			if (!Parsed.IsValidIndex(3))
			{
				ArkApi::GetApiUtils().SendServerMessage(player, FLinearColor(255, 0, 0), "Incorrect Syntax: /tpp <x> <y> <z>");
				return;
			}
			float X = 0, Y = 0, Z = 0;
			try
			{
				X = std::stof(Parsed[1].ToString().c_str());
				Y = std::stof(Parsed[2].ToString().c_str());
				Z = std::stof(Parsed[3].ToString().c_str());
			}
			catch (...) { return; }
			player->SetPlayerPos(X, Y, Z);
		}

		void ReloadConfig(AShooterPlayerController* player, FString* message, int mode)
		{
			if (!player || !player->PlayerStateField()() || !player->GetPlayerCharacter() || player->GetPlayerCharacter()->IsDead()) return;
			if (!player->GetPlayerCharacter()->bIsServerAdminField()())
			{
				ArkApi::GetApiUtils().SendServerMessage(player, FLinearColor(255, 0, 0), "Please login as admin to use this command");
				return;
			}
			SafeZoneManager::SZManager::Get().SafeZoneDistanceMap.clear();
			InitConfig();
			ArkApi::GetApiUtils().SendServerMessage(player, FLinearColor(0, 255, 0), "Config Reloaded!");
		}

		void ClaimItems(AShooterPlayerController* player, FString* message, int mode)
		{
			if (!player || !player->PlayerStateField()() || !player->GetPlayerCharacter() || player->GetPlayerCharacter()->IsDead()) return;
			const FVector Position = player->DefaultActorLocationField()();
			SafeZoneManager::SafeZoneDistanceSItr it = std::find_if(SafeZoneManager::SZManager::Get().SafeZoneDistanceMap.begin(), SafeZoneManager::SZManager::Get().SafeZoneDistanceMap.end(), [Position](SafeZoneManager::SafeZoneDistanceS& SZ) -> bool { return SZ.IsInArea(Position); });
			if (it != SafeZoneManager::SZManager::Get().SafeZoneDistanceMap.end() && it->Items.size() != 0)
			{
				for (SafeZoneManager::ItemS items : it->Items) player->GiveItem(&items.Blueprint, items.Quantity, items.Quailty, items.IsBlueprint);
				ArkApi::GetApiUtils().SendChatMessage(player, ServerName, *it->Messages[5]);
			}
		}
	#pragma endregion

	#pragma region Init
		void InitCommands()
		{
			ArkApi::GetCommands().AddChatCommand("/szsetpos", &SetPos);
			ArkApi::GetCommands().AddChatCommand("/szdist", &Dist);
			ArkApi::GetCommands().AddChatCommand("/sztp", &TPCoord);
			ArkApi::GetCommands().AddChatCommand("/szreload", &ReloadConfig);
			ArkApi::GetCommands().AddChatCommand(ClaimItemsCommand.c_str(), &ClaimItems);
		}
	#pragma endregion

	#pragma region Destroy
		void RemoveCommands()
		{
			ArkApi::GetCommands().RemoveChatCommand("/szsetpos");
			ArkApi::GetCommands().RemoveChatCommand("/szdist");
			ArkApi::GetCommands().RemoveChatCommand("/sztp");
			ArkApi::GetCommands().RemoveChatCommand("/szreload");
			ArkApi::GetCommands().RemoveChatCommand(ClaimItemsCommand.c_str());
		}
	#pragma endregion

#pragma endregion

#pragma region Main
void Init()
{
	Log::Get().Init("SafeZone");
	InitConfig();
	InitHooks();
	InitCommands();
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
		Init();
		break;
	case DLL_PROCESS_DETACH:
		RemoveHooks();
		RemoveCommands();
		break;
	}
	return TRUE;
}
#pragma endregion