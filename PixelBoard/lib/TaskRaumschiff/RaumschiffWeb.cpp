#include "RaumschiffWeb.h"

#include "Config.h"

String formatRaumschiffStateJson(const RaumschiffStatusSnapshot& s)
{
    String json = "{";
    json.reserve(1800);
    json += "\"state\":" + String((int)s.state);
    json += ",\"score\":" + String(s.score);
    json += ",\"web\":" + String(s.webConnected ? 1 : 0);
    json += ",\"webEnergy\":" + String(s.webEnergy);
    json += ",\"webEnergyMax\":" + String(s.webEnergyMax);
    json += ",\"p2InputMode\":" + String(s.p2InputMode);
    json += ",\"boss\":{\"active\":" + String(s.boss.active ? 1 : 0);
    json += ",\"hp\":" + String(s.boss.active ? s.boss.hp : 0);
    json += ",\"maxHp\":" + String(s.boss.maxHp);
    json += ",\"phase\":" + String(s.boss.phase);
    json += ",\"size\":" + String(s.boss.size) + "}";
    json += ",\"director\":{\"spawnMs\":" + String(s.directorSpawnMs);
    json += ",\"moveMs\":" + String(s.directorMoveMs);
    json += ",\"maxAsteroids\":" + String(s.directorMaxAsteroids);
    json += ",\"smallPercent\":" + String(s.directorSmallPercent);
    json += ",\"powerChance\":" + String(s.directorPowerChance);
    json += ",\"autoWeb\":" + String(s.directorAutoWeb ? 1 : 0);
    json += ",\"activeAsteroids\":" + String(s.activeAsteroids);
    json += ",\"activePowerUps\":" + String(s.activePowerUps);
    json += ",\"activeHazards\":" + String(s.activeHazards);
    json += ",\"slow\":" + String(s.slowSeconds) + "}";
    json += ",\"lastMatch\":{\"name\":\"" + jsonEscape(htmlEscape(String(s.lastMatchName))) + "\"";
    json += ",\"score\":" + String(s.lastMatchScore);
    json += ",\"p1\":" + String(s.lastMatchP1Score);
    json += ",\"p2\":" + String(s.lastMatchP2Score);
    json += ",\"p1Name\":\"" + jsonEscape(htmlEscape(String(s.players[0].name))) + "\"";
    json += ",\"p2Name\":\"" + jsonEscape(htmlEscape(String(s.players[1].name))) + "\"";
    json += ",\"p1Kills\":" + String(s.lastMatchP1Kills);
    json += ",\"p2Kills\":" + String(s.lastMatchP2Kills);
    json += ",\"bosses\":" + String(s.lastMatchBosses);
    json += ",\"duration\":" + String(s.lastMatchDurationSec) + "}";

    PlayerData top[3];
    int topCount = 0;
    getTopScores(top, topCount);
    json += ",\"top\":[";
    for (int i = 0; i < min(topCount, 3); i++) {
        if (i > 0) json += ',';
        json += "{\"name\":\"" + jsonEscape(htmlEscape(top[i].name)) + "\",\"score\":" + String(top[i].score) + "}";
    }
    json += "]";
    json += ",\"players\":[";
    for (uint8_t i = 0; i < RAUMSCHIFF_MAX_PLAYERS; i++) {
        const RaumschiffPlayerStatus& p = s.players[i];
        if (i > 0) json += ',';
        json += "{\"name\":\"" + jsonEscape(htmlEscape(String(p.name))) + "\"";
        json += ",\"shortName\":\"" + jsonEscape(htmlEscape(String(p.shortName))) + "\"";
        json += ",\"active\":" + String(p.active ? 1 : 0);
        json += ",\"alive\":" + String(p.alive ? 1 : 0);
        json += ",\"hp\":" + String(p.hp);
        json += ",\"score\":" + String(p.score);
        json += ",\"kills\":" + String(p.kills);
        json += ",\"level\":" + String(p.level);
        json += ",\"shield\":" + String(p.shield);
        json += ",\"multi\":" + String(p.multiSeconds);
        json += ",\"boost\":" + String(p.boostSeconds);
        json += ",\"invert\":" + String(p.invertSeconds);
        json += ",\"baseDamage\":" + String(p.baseDamage);
        json += ",\"damage\":" + String(p.damage);
        json += ",\"chargedDamage\":" + String(p.chargedDamage);
        json += ",\"nextDamageScore\":" + String(p.nextDamageScore);
        json += ",\"x\":" + String(p.x);
        json += ",\"y\":" + String(p.y) + "}";
    }
    json += "]}";
    return json;
}
