#if defined _momsurffix_ext2_included
	#endinput
#endif
#define _momsurffix_ext2_included

forward void MomSurfFix2_OnClipVelocity(int client, const float inVel[3],
                                         const float planeNormal[3], const float outVel[3]);

native bool MomSurfFix2_GetLastClipData(int client, float inVel[3],
                                         float planeNormal[3], float outVel[3]);

native bool MomSurfFix2_GetFixStats(int &totalFixes, float &avgLoss, float &avgGain);

public Extension __ext_momsurffix2 =
{
	name = "MomSurfFix Extension 2",
	file = "momsurffix2_ext.ext",
	autoload = 0,
#if defined REQUIRE_EXTENSIONS
	required = 1,
#else
	required = 0,
#endif
};

#if !defined REQUIRE_EXTENSIONS
public void __ext_momsurffix2_SetNTVOptional()
{
	MarkNativeAsOptional("MomSurfFix2_GetLastClipData");
	MarkNativeAsOptional("MomSurfFix2_GetFixStats");
}
#endif
