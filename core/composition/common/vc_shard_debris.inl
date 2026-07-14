#define ARCH_MAX_DEBRIS_SHARDS 128

typedef struct {
    bool active;
    Vector3 position;
    Vector3 velocity;
    Vector3 rotationAxis;
    float rotationAngle;
    float rotationSpeed;
    float scale;
    float elapsed;
    float lifetime;
    int seed;
    MaterialPreset matPreset;
    VC_MaterialId matId;
} Arch_DebrisShard;

static Arch_DebrisShard s_archDebrisShards[ARCH_MAX_DEBRIS_SHARDS];

static void DrawUnitBoxJittered(int seed, Color color)
{
    float ox[8], oy[8], oz[8];
    unsigned int nextSeed = (unsigned int)seed;
    for (int i = 0; i < 8; i++)
    {
        nextSeed = nextSeed * 1103515245 + 12345;
        ox[i] = (((float)(nextSeed / 65536 % 32768) / 32767.0f) - 0.5f) * 0.35f;
        nextSeed = nextSeed * 1103515245 + 12345;
        oy[i] = (((float)(nextSeed / 65536 % 32768) / 32767.0f) - 0.5f) * 0.35f;
        nextSeed = nextSeed * 1103515245 + 12345;
        oz[i] = (((float)(nextSeed / 65536 % 32768) / 32767.0f) - 0.5f) * 0.35f;
    }

    Vector3 v[8] = {
        {-0.5f + ox[0], -0.5f + oy[0], -0.5f + oz[0]},
        { 0.5f + ox[1], -0.5f + oy[1], -0.5f + oz[1]},
        { 0.5f + ox[2],  0.5f + oy[2], -0.5f + oz[2]},
        {-0.5f + ox[3],  0.5f + oy[3], -0.5f + oz[3]},
        {-0.5f + ox[4], -0.5f + oy[4],  0.5f + oz[4]},
        { 0.5f + ox[5], -0.5f + oy[5],  0.5f + oz[5]},
        { 0.5f + ox[6],  0.5f + oy[6],  0.5f + oz[6]},
        {-0.5f + ox[7],  0.5f + oy[7],  0.5f + oz[7]}
    };

    rlColor4ub(color.r, color.g, color.b, color.a);
    rlBegin(RL_TRIANGLES);

    int faces[6][4] = {
        {0, 3, 2, 1}, 
        {1, 2, 6, 5}, 
        {5, 6, 7, 4}, 
        {4, 7, 3, 0}, 
        {3, 7, 6, 2}, 
        {4, 0, 1, 5}  
    };

    for (int f = 0; f < 6; f++)
    {
        Vector3 edge1 = Vector3Subtract(v[faces[f][1]], v[faces[f][0]]);
        Vector3 edge2 = Vector3Subtract(v[faces[f][3]], v[faces[f][0]]);
        Vector3 normal = Vector3Normalize(Vector3CrossProduct(edge2, edge1));

        rlNormal3f(normal.x, normal.y, normal.z);
        rlVertex3f(v[faces[f][0]].x, v[faces[f][0]].y, v[faces[f][0]].z);
        rlVertex3f(v[faces[f][1]].x, v[faces[f][1]].y, v[faces[f][1]].z);
        rlVertex3f(v[faces[f][2]].x, v[faces[f][2]].y, v[faces[f][2]].z);
        rlVertex3f(v[faces[f][0]].x, v[faces[f][0]].y, v[faces[f][0]].z);
        rlVertex3f(v[faces[f][2]].x, v[faces[f][2]].y, v[faces[f][2]].z);
        rlVertex3f(v[faces[f][3]].x, v[faces[f][3]].y, v[faces[f][3]].z);
    }

    rlEnd();
}

void VFX_ComposeShardDebris(Vector3 pos, int count, float speed, VC_MaterialId matId)
{
    if (count <= 0)
        return;
    if (count > 12)
        count = 12;

    MaterialPreset matPreset = MAT_ROCK;
    switch (matId)
    {
        case VC_MAT_FIRE:      matPreset = MAT_FIRE; break;
        case VC_MAT_ICE:       matPreset = MAT_ICE; break;
        case VC_MAT_WATER:     matPreset = MAT_WATER; break;
        case VC_MAT_METAL:     matPreset = MAT_METAL; break;
        case VC_MAT_VOID:      matPreset = MAT_PORTAL; break;
        default:               matPreset = MAT_ROCK; break;
    }

    int spawned = 0;
    for (int i = 0; i < ARCH_MAX_DEBRIS_SHARDS && spawned < count; i++)
    {
        if (s_archDebrisShards[i].active)
            continue;

        s_archDebrisShards[i].active = true;
        s_archDebrisShards[i].position = pos;
        
        float yaw = (float)rand() / (float)RAND_MAX * 2.0f * PI;
        float pitch = ((float)rand() / (float)RAND_MAX * 70.0f + 20.0f) * DEG2RAD; 
        Vector3 dir = {
            cosf(yaw) * cosf(pitch),
            sinf(pitch),
            sinf(yaw) * cosf(pitch)
        };
        dir = Vector3Normalize(dir);
        
        float randSpeed = speed * (0.6f + ((float)rand() / (float)RAND_MAX * 0.8f));
        s_archDebrisShards[i].velocity = Vector3Scale(dir, randSpeed);

        s_archDebrisShards[i].rotationAxis = Vector3Normalize((Vector3){
            (float)rand() / (float)RAND_MAX * 2.0f - 1.0f,
            (float)rand() / (float)RAND_MAX * 2.0f - 1.0f,
            (float)rand() / (float)RAND_MAX * 2.0f - 1.0f
        });
        s_archDebrisShards[i].rotationAngle = (float)rand() / (float)RAND_MAX * 360.0f;
        s_archDebrisShards[i].rotationSpeed = 180.0f + ((float)rand() / (float)RAND_MAX * 540.0f); 

        s_archDebrisShards[i].scale = 0.02f + ((float)rand() / (float)RAND_MAX * 0.04f); 
        s_archDebrisShards[i].lifetime = 0.4f + ((float)rand() / (float)RAND_MAX * 0.4f); 
        s_archDebrisShards[i].elapsed = 0.0f;
        s_archDebrisShards[i].seed = rand() % 500;
        s_archDebrisShards[i].matPreset = matPreset;
        s_archDebrisShards[i].matId = matId;

        spawned++;
    }
}

static void VC_ShardDebris_Update(float dt)
{
    for (int i = 0; i < ARCH_MAX_DEBRIS_SHARDS; i++) {
        if (!s_archDebrisShards[i].active) continue;
        s_archDebrisShards[i].elapsed += dt;
        if (s_archDebrisShards[i].elapsed >= s_archDebrisShards[i].lifetime) {
            s_archDebrisShards[i].active = false;
            continue;
        }

        s_archDebrisShards[i].velocity.y -= 9.81f * dt;
        s_archDebrisShards[i].velocity = Vector3Scale(s_archDebrisShards[i].velocity, 1.0f - 0.4f * dt);
        s_archDebrisShards[i].position = Vector3Add(s_archDebrisShards[i].position, 
                                                    Vector3Scale(s_archDebrisShards[i].velocity, dt));
        s_archDebrisShards[i].rotationAngle += s_archDebrisShards[i].rotationSpeed * dt;

        if (GetRandomValue(0, 100) < 25) {
            const VFX_ElementMaterial *eMat = VFX_Material(s_archDebrisShards[i].matId);
            if (eMat && eMat->grad) {
                const ColorGradient *partGrad = eMat->grad;
                float partRad = s_archDebrisShards[i].scale * 0.15f + Random01() * 0.01f;
                if (s_archDebrisShards[i].matId == VC_MAT_ICE || s_archDebrisShards[i].matId == VC_MAT_METAL) {
                    ShardSparkle_Init();
                    partGrad = &s_shardSparkleGrad;
                    partRad = s_archDebrisShards[i].scale * 0.25f + Random01() * 0.02f;
                }
                SpawnParticle((ParticleConfig){
                    .position = s_archDebrisShards[i].position,
                    .velocity = (Vector3){(Random01() - 0.5f) * 0.2f, (Random01() - 0.5f) * 0.2f, (Random01() - 0.5f) * 0.2f},
                    .radius = partRad,
                    .lifetime = 0.12f + Random01() * 0.18f,
                    .gradient = partGrad
                });
            }
        }

        if (s_archDebrisShards[i].position.y < 0.0f) {
            s_archDebrisShards[i].position.y = 0.0f;
            s_archDebrisShards[i].velocity.y = -s_archDebrisShards[i].velocity.y * 0.45f;
            s_archDebrisShards[i].velocity.x *= 0.6f;
            s_archDebrisShards[i].velocity.z *= 0.6f;
            s_archDebrisShards[i].rotationSpeed *= 0.7f;

            const VFX_ElementMaterial *eMat = VFX_Material(s_archDebrisShards[i].matId);
            if (eMat && eMat->grad) {
                const ColorGradient *partGrad = eMat->grad;
                if (s_archDebrisShards[i].matId == VC_MAT_ICE || s_archDebrisShards[i].matId == VC_MAT_METAL) {
                    ShardSparkle_Init();
                    partGrad = &s_shardSparkleGrad;
                }
                for (int p = 0; p < 2; p++) {
                    SpawnParticle((ParticleConfig){
                        .position = s_archDebrisShards[i].position,
                        .velocity = (Vector3){
                            (Random01() - 0.5f) * 1.2f,
                            Random01() * 0.8f,
                            (Random01() - 0.5f) * 1.2f
                        },
                        .radius = s_archDebrisShards[i].scale * 0.2f + Random01() * 0.015f,
                        .lifetime = 0.15f + Random01() * 0.15f,
                        .gradient = partGrad
                    });
                }
            }

            if (s_archDebrisShards[i].velocity.y < 0.25f) {
                s_archDebrisShards[i].velocity.y = 0.0f;
                s_archDebrisShards[i].velocity.x = 0.0f;
                s_archDebrisShards[i].velocity.z = 0.0f;
                s_archDebrisShards[i].rotationSpeed = 0.0f;
            }
        }
    }
}

static void VC_ShardDebris_Draw3D(Camera3D cam)
{
    (void)cam;
    rlDisableBackfaceCulling();
    MaterialPreset currentPreset = -1;
    float currentDissolve = -1.0f;

    for (int i = 0; i < ARCH_MAX_DEBRIS_SHARDS; i++) {
        if (!s_archDebrisShards[i].active) continue;

        MaterialPreset shardPreset = s_archDebrisShards[i].matPreset;
        float progress = s_archDebrisShards[i].elapsed / s_archDebrisShards[i].lifetime;

        float shardDissolve = 0.0f;
        if (progress >= 0.8f) {
            shardDissolve = (progress - 0.8f) / 0.2f;
        }

        if (shardPreset != currentPreset || fabsf(shardDissolve - currentDissolve) > 0.05f) {
            if (currentPreset != -1) {
                EffectMaterial prevMat;
                Material_Get(&prevMat, currentPreset);
                Material_SetFloat(&prevMat, "u_dissolve", 0.0f);
                Material_End();
            }
            currentPreset = shardPreset;
            currentDissolve = shardDissolve;
            EffectMaterial mat;
            Material_Get(&mat, currentPreset);
            Material_Begin(mat);
            Material_SetFloat(&mat, "u_dissolve", currentDissolve);
        }

        rlPushMatrix();
        rlTranslatef(s_archDebrisShards[i].position.x, 
                     s_archDebrisShards[i].position.y, 
                     s_archDebrisShards[i].position.z);
        rlRotatef(s_archDebrisShards[i].rotationAngle, 
                  s_archDebrisShards[i].rotationAxis.x, 
                  s_archDebrisShards[i].rotationAxis.y, 
                  s_archDebrisShards[i].rotationAxis.z);
        rlScalef(s_archDebrisShards[i].scale, s_archDebrisShards[i].scale, s_archDebrisShards[i].scale);

        DrawUnitBoxJittered(s_archDebrisShards[i].seed, WHITE);
        
        rlPopMatrix();
    }

    if (currentPreset != -1) {
        EffectMaterial prevMat;
        Material_Get(&prevMat, currentPreset);
        Material_SetFloat(&prevMat, "u_dissolve", 0.0f);
        Material_End();
    }
    rlEnableBackfaceCulling();
}
