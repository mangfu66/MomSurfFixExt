#if defined _momsurffix_ext_included
	#endinput
#endif
#define _momsurffix_ext_included

/**
 * Called when ClipVelocity fix is applied.
 *
 * @param client        Player index.
 * @param inVel         Velocity before clipping.
 * @param planeNormal   Plane normal used for the clip.
 * @param outVel        Velocity after clipping (fixed).
 */
forward void MomSurfFix_OnClipVelocity(int client, const float inVel[3],
                                        const float planeNormal[3], const float outVel[3]);

/**
 * Get the last ClipVelocity data for a client.
 *
 * @param client        Player index.
 * @param inVel         Buffer for velocity before clipping.
 * @param planeNormal   Buffer for plane normal.
 * @param outVel        Buffer for velocity after clipping.
 */
native bool MomSurfFix_GetLastClipData(int client, float inVel[3],
                                        float planeNormal[3], float outVel[3]);

/**
 * Get global fix statistics.
 *
 * @param totalFixes    Total number of fixes applied.
 * @param avgLoss       Average speed loss per fix (u/s).
 * @param avgGain       Average speed gain per fix (u/s).
 */
native bool MomSurfFix_GetFixStats(int &totalFixes, float &avgLoss, float &avgGain);

public Extension __ext_momsurffix =
{
	name = "MomSurfFix Extension",
	file = "momsurffix_ext.ext",
	autoload = 0,
#if defined REQUIRE_EXTENSIONS
	required = 1,
#else
	required = 0,
#endif
};

#if !defined REQUIRE_EXTENSIONS
public void __ext_momsurffix_SetNTVOptional()
{
	MarkNativeAsOptional("MomSurfFix_GetLastClipData");
	MarkNativeAsOptional("MomSurfFix_GetFixStats");
}
#endif
