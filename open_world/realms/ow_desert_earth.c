// open_world/realms/ow_desert_earth.c
#include "open_world/realms/ow_desert_earth.h"

static const OWRealmDef kDesertEarthRealm = {
    .realmId         = 3,
    .safeCenter      = { 170.68f, 0.0f, 230.41f },
    .safeRadius      = 22.0f,
    .daTauNpcPos     = { 163.5f,  0.0f, 224.0f  },
    .alchemistPos    = { 168.0f,  0.0f, 226.5f  },
    .stelePos        = { 157.5f,  0.0f, 219.0f  },
    .defaultStartPos = { 160.0f,  0.0f, 222.0f  },
    .dunesCenter     = { 135.0f,  0.0f, 195.0f  },
    .canyonCenter    = { 94.10f,  0.0f, 150.0f  },
    .badlandsCenter  = { 65.0f,   0.0f, 95.0f   },
    .zoneCount = 4,
    .zones = {
        { { 170.68f, 0.0f, 230.41f }, 22.0f, "OC DAO HOANG SA [AN TOAN]" },
        { { 135.0f,  0.0f, 195.0f  }, 32.0f, "DOI CAT PHONG SA [TAN THU]" },
        { { 94.10f,  0.0f, 150.0f  }, 45.0f, "THACH LAM CO DAO [GIANG HO]" },
        { { 65.0f,   0.0f, 95.0f   }, 40.0f, "HUYET DAO HAC DIEN [HUNG HIEM]" }
    },
    .nodeCount = 16,
    .nodes = {
        // 6 Oasis Herbs around safe shore
        { OW_NODE_HERB, 184.0f, 236.0f, true },
        { OW_NODE_HERB, 176.0f, 244.0f, true },
        { OW_NODE_HERB, 160.0f, 241.0f, true },
        { OW_NODE_HERB, 156.0f, 225.0f, true },
        { OW_NODE_HERB, 165.0f, 217.0f, true },
        { OW_NODE_HERB, 179.0f, 219.0f, true },
        // 6 Earth Ore Veins
        { OW_NODE_ORE_VEIN, 144.0f, 206.0f, true },
        { OW_NODE_ORE_VEIN, 128.0f, 188.0f, true },
        { OW_NODE_ORE_VEIN, 108.0f, 166.0f, true },
        { OW_NODE_ORE_VEIN, 94.0f,  144.0f, true },
        { OW_NODE_ORE_VEIN, 82.0f,  158.0f, true },
        { OW_NODE_ORE_VEIN, 78.0f,  126.0f, true },
        // 4 Buried & Golden Relic Chests
        { OW_NODE_BURIED_CHEST,       122.0f, 178.0f, true },
        { OW_NODE_BURIED_CHEST,       98.0f,  132.0f, true },
        { OW_NODE_GOLDEN_RELIC_CHEST, 72.0f,  104.0f, true },
        { OW_NODE_GOLDEN_RELIC_CHEST, 58.0f,  88.0f,  false }
    },
    .windSurgeCount = 4,
    .windSurges = {
        { 149.0f, 0.0f, 210.0f },
        { 116.0f, 0.0f, 174.0f },
        { 105.0f, 0.0f, 138.0f },
        { 80.0f,  0.0f, 128.0f }
    },
    .campCount = 5,
    .camps = {
        { { 136.0f, 0.0f, 196.0f }, 4, 3, MONSTER_SWARM_MELEE },
        { { 126.0f, 0.0f, 184.0f }, 3, 3, MONSTER_SWARM_MELEE },
        { { 96.0f,  0.0f, 152.0f }, 3, 3, MONSTER_RANGED_CASTER },
        { { 84.0f,  0.0f, 138.0f }, 3, 3, MONSTER_SWARM_MELEE },
        { { 66.0f,  0.0f, 96.0f  }, 2, 3, MONSTER_ELITE_GUARD }
    }
};

const OWRealmDef *OWRealm_GetDesertEarth(void) {
    return &kDesertEarthRealm;
}
