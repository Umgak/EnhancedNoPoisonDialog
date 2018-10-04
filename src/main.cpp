#include "skse64/PluginAPI.h"
#include "skse64/GameObjects.h"
#include "skse64_common/Relocation.h"
#include "skse64_common/SafeWrite.h"
#include "skse64_common/skse_version.h"
#include "skse64_common/BranchTrampoline.h"
#include "xbyak/xbyak.h"
#include <shlobj.h>

IDebugLog	gLog;
PluginHandle	g_pluginHandle = kPluginHandle_Invalid;

void * g_moduleHandle = nullptr;

RelocPtr<uintptr_t> GetEquippedWeaponAddr1(0x006A1EDF);  // 1_5_53
RelocPtr<uintptr_t> GetEquippedWeaponAddr2(0x006A2052);  // 1_5_53
RelocPtr<uintptr_t> ApplyPoisonAddr(0x006A1F4C);  // 1_5_53
RelocPtr<uintptr_t> DebugNotif_HookAddr(0x006A1FC9);  // 1_5_53

typedef void* GetEquippedWeapon_t(ActorProcessManager * apm, bool isLeftHand);
RelocAddr<GetEquippedWeapon_t*> GetEquippedWeapon(0x0067A5D0);  // 1_5_53

typedef AlchemyItem* GetExtraPoison_t(void * unk);
RelocAddr<GetExtraPoison_t*> GetExtraPoison(0x001D6BB0);  // 1_5_53

typedef void ApplyPoison_t(bool check);
RelocAddr<ApplyPoison_t*> ApplyPoison(0x006A2020);  // 1_5_53

typedef void DebugNotification_t(const char *, bool, bool);
RelocAddr<DebugNotification_t*> DebugNotification(0x008DA5C0);  // 1_5_53


static void* GetEquippedWeapon_Hook(ActorProcessManager * apm, bool isLeftHand)
{
	void * left = GetEquippedWeapon(apm, !isLeftHand);
	void * right = GetEquippedWeapon(apm, isLeftHand); // this is internal version of GetEquippedWeapon, so the return value is not a TESObjectWEAP *
	if (right && left)
	{
		// check if it is already poisoned
		if (GetExtraPoison(right))
			return GetEquippedWeapon(apm, true); // apply to left
		else
			return GetEquippedWeapon(apm, false); // apply to right
	}
	else if (right)
	{
		return GetEquippedWeapon(apm, false); // apply to right
	}
	else
		return GetEquippedWeapon(apm, true); // apply to left

}

extern "C"
{

	bool SKSEPlugin_Query(const SKSEInterface * skse, PluginInfo * info)
	{

		gLog.OpenRelative(CSIDL_MYDOCUMENTS, "\\My Games\\Skyrim Special Edition\\SKSE\\EnhancedNoPoisonDialog.log");

		info->infoVersion = PluginInfo::kInfoVersion;
		info->name = "EnhancedNoPoisonDialog plugin";
		info->version = 1;

		g_pluginHandle = skse->GetPluginHandle();

		if (skse->isEditor)
		{
			_MESSAGE("loaded in editor, marking as incompatible");
			return false;
		}

		if (skse->runtimeVersion != RUNTIME_VERSION_1_5_53)
		{
			_MESSAGE("This plugin is not compatible with this versin of game.");
			return false;
		}

		if (!g_branchTrampoline.Create(1024 * 64))
		{
			_ERROR("couldn't create branch trampoline. this is fatal. skipping remainder of init process.");
			return false;
		}

		if (!g_localTrampoline.Create(1024 * 64, g_moduleHandle))
		{
			_ERROR("couldn't create codegen buffer. this is fatal. skipping remainder of init process.");
			return false;
		}

		return true;
	}

	bool SKSEPlugin_Load(const SKSEInterface * skse)
	{
		_MESSAGE("Load");

		g_branchTrampoline.Write5Call(GetEquippedWeaponAddr1.GetUIntPtr(), GetFnAddr(&GetEquippedWeapon_Hook));
		g_branchTrampoline.Write5Call(GetEquippedWeaponAddr2.GetUIntPtr(), GetFnAddr(&GetEquippedWeapon_Hook));

		{
			struct InstallHookApplyPoison_Code : Xbyak::CodeGenerator {
				InstallHookApplyPoison_Code(void * buf, uintptr_t funcAddr) : Xbyak::CodeGenerator(4096, buf)
				{
					Xbyak::Label retnLabel;
					Xbyak::Label funcLabel;

					mov(ptr[rsi + 0x968], rbx);

					sub(rsp, 0x20);

					mov(cl, 2);
					call(ptr[rip + funcLabel]);

					add(rsp, 0x20);

					jmp(ptr[rip + retnLabel]);

					L(funcLabel);
					dq(funcAddr);

					L(retnLabel);
					dq(ApplyPoisonAddr.GetUIntPtr() + 0x74);
				}
			};

			void * codeBuf = g_localTrampoline.StartAlloc();
			InstallHookApplyPoison_Code code(codeBuf, GetFnAddr(ApplyPoison.GetUIntPtr()));
			g_localTrampoline.EndAlloc(code.getCurr());

			if (!g_branchTrampoline.Write5Branch(ApplyPoisonAddr.GetUIntPtr(), uintptr_t(code.getCode())))
				return false;
			SafeWrite16(ApplyPoisonAddr.GetUIntPtr() + 0x5, 0x9090);
		}


		// Replace the debug messagebox with debug notif
		{
			struct InstallHookDebugNotif_Code : Xbyak::CodeGenerator {
				InstallHookDebugNotif_Code(void * buf, uintptr_t funcAddr) : Xbyak::CodeGenerator(4096, buf)
				{
					Xbyak::Label retnLabel;
					Xbyak::Label funcLabel;

					sub(rsp, 0x20);

					mov(r8b, 1);
					xor(rdx, rdx);
					call(ptr[rip + funcLabel]);

					add(rsp, 0x20);

					jmp(ptr[rip + retnLabel]);

					L(funcLabel);
					dq(funcAddr);

					L(retnLabel);
					dq(DebugNotif_HookAddr.GetUIntPtr() + 0x2F);
				}
			};

			void * codeBuf = g_localTrampoline.StartAlloc();
			InstallHookDebugNotif_Code code(codeBuf, GetFnAddr(DebugNotification.GetUIntPtr()));
			g_localTrampoline.EndAlloc(code.getCurr());

			if (!g_branchTrampoline.Write5Branch(DebugNotif_HookAddr.GetUIntPtr(), uintptr_t(code.getCode())))
				return false;
			SafeWrite16(DebugNotif_HookAddr.GetUIntPtr() + 0x5, 0x9090);
		}

		return true;
	}

}