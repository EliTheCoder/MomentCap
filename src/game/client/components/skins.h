/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_COMPONENTS_SKINS_H
#define GAME_CLIENT_COMPONENTS_SKINS_H
#include <base/color.h>
#include <base/tl/sorted_array.h>
#include <base/vmath.h>
#include <game/client/component.h>
#include <game/client/skin.h>

class CSkins : public CComponent
{
public:
	void OnInit();

	void Refresh();
	int Num();
	const CSkin *Get(int Index);
	int Find(const char *pName);

private:
	sorted_array<CSkin> m_aSkins;
	char m_EventSkinPrefix[24];

	bool LoadSkinPNG(CImageInfo &Info, const char *pName, const char *pPath, int DirType);
	int LoadSkin(const char *pName, const char *pPath, int DirType);
	int LoadSkin(const char *pName, CImageInfo &Info);
	int FindImpl(const char *pName);
	static int SkinScan(const char *pName, int IsDir, int DirType, void *pUser);
};
#endif
