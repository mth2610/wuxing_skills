sed -i '' '/static Shader defaultShader;/i\
static Texture2D s_globalTrailTex = {0};\
void TrailSystem_SetGlobalTexture(Texture2D tex) { s_globalTrailTex = tex; }\
' /Users/mth2610/Desktop/c_games/wuxing_skills/core/trail_system.c
