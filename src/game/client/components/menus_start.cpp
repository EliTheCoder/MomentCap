/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <engine/graphics.h>
#include <engine/keys.h>
#include <engine/textrender.h>

#include <engine/shared/updater_stub.h>
#include <engine/shared/config.h>

#include <game/client/components/console.h>
#include <game/client/render.h>
#include <game/client/ui.h>
#include <game/editor/editor.h>
#include <game/version.h>

#include "menus.h"

void CMenus::RenderStartMenu(CUIRect MainView)
{
	// render logo
	Graphics()->TextureSet(g_pData->m_aImages[IMAGE_BANNER].m_Id);
	Graphics()->QuadsBegin();
	Graphics()->SetColor(1, 1, 1, 1);
	IGraphics::CQuadItem QuadItem(MainView.w / 2 - 170, 60, 360, 103);
	Graphics()->QuadsDrawTL(&QuadItem, 1);
	Graphics()->QuadsEnd();

	const float Rounding = 10.0f;
	const float VMargin = MainView.w / 2 - 190.0f;

	CUIRect Button;
	int NewPage = -1;

	CUIRect ExtMenu;
	MainView.VSplitLeft(30.0f, 0, &ExtMenu);
	ExtMenu.VSplitLeft(100.0f, &ExtMenu, 0);

	ExtMenu.HSplitBottom(20.0f, &ExtMenu, &Button);
	static int s_DiscordButton;
	if(DoButton_Menu(&s_DiscordButton, "Discord", 0, &Button, 0, CUI::CORNER_ALL, 5.0f, 0.0f, vec4(0.0f, 0.0f, 0.0f, 0.5f), vec4(0.0f, 0.0f, 0.0f, 0.25f)))
	{
		if(!open_link("https://ddnet.tw/discord"))
		{
			dbg_msg("menus", "couldn't open link");
		}
		m_DoubleClickIndex = -1;
	}

	ExtMenu.HSplitBottom(5.0f, &ExtMenu, 0); // little space
	ExtMenu.HSplitBottom(20.0f, &ExtMenu, &Button);
	static int s_LearnButton;
	if(DoButton_Menu(&s_LearnButton, Localize("Learn"), 0, &Button, 0, CUI::CORNER_ALL, 5.0f, 0.0f, vec4(0.0f, 0.0f, 0.0f, 0.5f), vec4(0.0f, 0.0f, 0.0f, 0.25f)))
	{
		if(!open_link(Localize("https://wiki.ddnet.tw/")))
		{
			dbg_msg("menus", "couldn't open link");
		}
		m_DoubleClickIndex = -1;
	}

	ExtMenu.HSplitBottom(5.0f, &ExtMenu, 0); // little space
	ExtMenu.HSplitBottom(20.0f, &ExtMenu, &Button);
	static int s_WebsiteButton;
	if(DoButton_Menu(&s_WebsiteButton, Localize("Website"), 0, &Button, 0, CUI::CORNER_ALL, 5.0f, 0.0f, vec4(0.0f, 0.0f, 0.0f, 0.5f), vec4(0.0f, 0.0f, 0.0f, 0.25f)))
	{
		if(!open_link("https://ddnet.tw/"))
		{
			dbg_msg("menus", "couldn't open link");
		}
		m_DoubleClickIndex = -1;
	}

	ExtMenu.HSplitBottom(5.0f, &ExtMenu, 0); // little space
	ExtMenu.HSplitBottom(20.0f, &ExtMenu, &Button);
	static int s_NewsButton;
	if(DoButton_Menu(&s_NewsButton, Localize("News"), 0, &Button, 0, CUI::CORNER_ALL, 5.0f, 0.0f, vec4(0.0f, 0.0f, 0.0f, 0.5f), g_Config.m_UiUnreadNews ? vec4(0.0f, 1.0f, 0.0f, 0.25f) : vec4(0.0f, 0.0f, 0.0f, 0.25f)) || CheckHotKey(KEY_N))
		NewPage = PAGE_NEWS;

	CUIRect Menu;
	MainView.VMargin(VMargin, &Menu);
	Menu.HSplitBottom(25.0f, &Menu, 0);

	Menu.HSplitBottom(40.0f, &Menu, &Button);
	static int s_QuitButton;
	if(DoButton_Menu(&s_QuitButton, Localize("Quit"), 0, &Button, 0, CUI::CORNER_ALL, Rounding, 0.5f, vec4(0.0f, 0.0f, 0.0f, 0.5f), vec4(0.0f, 0.0f, 0.0f, 0.25f)) || m_EscapePressed || CheckHotKey(KEY_Q))
	{
		if(m_EscapePressed || m_pClient->Editor()->HasUnsavedData() || (Client()->GetCurrentRaceTime() / 60 >= g_Config.m_ClConfirmQuitTime && g_Config.m_ClConfirmQuitTime >= 0))
		{
			m_Popup = POPUP_QUIT;
		}
		else
		{
			Client()->Quit();
		}
	}

	Menu.HSplitBottom(100.0f, &Menu, 0);
	Menu.HSplitBottom(40.0f, &Menu, &Button);
	static int s_SettingsButton;
	if(DoButton_Menu(&s_SettingsButton, Localize("Settings"), 0, &Button, g_Config.m_ClShowStartMenuImages ? "settings" : 0, CUI::CORNER_ALL, Rounding, 0.5f, vec4(0.0f, 0.0f, 0.0f, 0.5f), vec4(0.0f, 0.0f, 0.0f, 0.25f)) || CheckHotKey(KEY_S))
		NewPage = PAGE_SETTINGS;

	Menu.HSplitBottom(5.0f, &Menu, 0); // little space
	Menu.HSplitBottom(40.0f, &Menu, &Button);
	static int s_LocalServerButton = 0;
	if(DoButton_Menu(&s_LocalServerButton, m_ServerProcess.Process ? Localize("Stop server") : Localize("Run server"), 0, &Button, g_Config.m_ClShowStartMenuImages ? "local_server" : 0, CUI::CORNER_ALL, Rounding, 0.5f, vec4(0.0f, 0.0f, 0.0f, 0.5f), m_ServerProcess.Process ? vec4(0.0f, 1.0f, 0.0f, 0.25f) : vec4(0.0f, 0.0f, 0.0f, 0.25f)) || (CheckHotKey(KEY_R) && Input()->KeyPress(KEY_R)))
	{
		if(m_ServerProcess.Process)
		{
			KillServer();
		}
		else
		{
			char aBuf[MAX_PATH_LENGTH];
			Storage()->GetBinaryPath(PLAT_SERVER_EXEC, aBuf, sizeof(aBuf));
			IOHANDLE File = io_open(aBuf, IOFLAG_READ);
			if(File)
			{
				io_close(File);
				m_ServerProcess.Process = shell_execute(aBuf);
			}
			else
			{
				PopupWarning(Localize("Warning"), Localize("Server executable not found, can't run server"), Localize("Ok"), 10000000);
			}
		}
	}

	static bool EditorHotkeyWasPressed = true;
	static float EditorHotKeyChecktime = 0;
	Menu.HSplitBottom(5.0f, &Menu, 0); // little space
	Menu.HSplitBottom(40.0f, &Menu, &Button);
	static int s_MapEditorButton;
	if(DoButton_Menu(&s_MapEditorButton, Localize("Editor"), 0, &Button, g_Config.m_ClShowStartMenuImages ? "editor" : 0, CUI::CORNER_ALL, Rounding, 0.5f, vec4(0.0f, 0.0f, 0.0f, 0.5f), m_pClient->Editor()->HasUnsavedData() ? vec4(0.0f, 1.0f, 0.0f, 0.25f) : vec4(0.0f, 0.0f, 0.0f, 0.25f)) || (!EditorHotkeyWasPressed && Client()->LocalTime() - EditorHotKeyChecktime < 0.1f && CheckHotKey(KEY_E)))
	{
		g_Config.m_ClEditor = 1;
		Input()->MouseModeRelative();
		EditorHotkeyWasPressed = true;
	}
	if(!Input()->KeyIsPressed(KEY_E))
	{
		EditorHotkeyWasPressed = false;
		EditorHotKeyChecktime = Client()->LocalTime();
	}

	Menu.HSplitBottom(5.0f, &Menu, 0); // little space
	Menu.HSplitBottom(40.0f, &Menu, &Button);
	static int s_DemoButton;
	if(DoButton_Menu(&s_DemoButton, Localize("Demos"), 0, &Button, g_Config.m_ClShowStartMenuImages ? "demos" : 0, CUI::CORNER_ALL, Rounding, 0.5f, vec4(0.0f, 0.0f, 0.0f, 0.5f), vec4(0.0f, 0.0f, 0.0f, 0.25f)) || CheckHotKey(KEY_D))
	{
		NewPage = PAGE_DEMOS;
	}

	Menu.HSplitBottom(5.0f, &Menu, 0); // little space
	Menu.HSplitBottom(40.0f, &Menu, &Button);
	static int s_PlayButton;
	if(DoButton_Menu(&s_PlayButton, Localize("Play", "Start menu"), 0, &Button, g_Config.m_ClShowStartMenuImages ? "play_game" : 0, CUI::CORNER_ALL, Rounding, 0.5f, vec4(0.0f, 0.0f, 0.0f, 0.5f), vec4(0.0f, 0.0f, 0.0f, 0.25f)) || m_EnterPressed || CheckHotKey(KEY_P))
	{
		NewPage = g_Config.m_UiPage >= PAGE_INTERNET && g_Config.m_UiPage <= PAGE_KOG ? g_Config.m_UiPage : PAGE_DDNET;
	}

	// render version
	CUIRect VersionUpdate, CurVersion;
	MainView.HSplitBottom(30.0f, 0, 0);
	MainView.HSplitBottom(20.0f, 0, &VersionUpdate);

	VersionUpdate.VSplitRight(50.0f, &CurVersion, 0);
	VersionUpdate.VMargin(VMargin, &VersionUpdate);

	UI()->DoLabel(&CurVersion, GAME_RELEASE_VERSION, 14.0f, 1);

	if(NewPage != -1)
	{
		m_MenuPage = NewPage;
		m_ShowStart = false;
	}
}

void CMenus::KillServer()
{
	if(m_ServerProcess.Process)
	{
		kill_process(m_ServerProcess.Process);
		m_ServerProcess.Process = 0;
	}
}
