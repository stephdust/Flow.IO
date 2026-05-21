/**
 * @file SupervisorHmiTextSet.cpp
 * @brief Built-in fr/en localized strings for Supervisor TFT.
 */

#include "Modules/SupervisorHMIModule/SupervisorHmiTextSet.h"

namespace {

bool isLangCode_(const char* value, char c0, char c1)
{
    if (!value) return false;
    char a = value[0];
    char b = value[1];
    if (a >= 'A' && a <= 'Z') a = (char)(a + ('a' - 'A'));
    if (b >= 'A' && b <= 'Z') b = (char)(b + ('a' - 'A'));
    if (a != c0 || b != c1) return false;
    const char end = value[2];
    return end == '\0' || end == '-' || end == '_' || end == '.';
}

static const SupervisorHmiTextSet kTextFr{
    {
        "PSI bas",
        "PSI haut",
        "pH vide",
        "Chlore vide",
        "pH uptime",
        "ORP uptime",
        "Eau basse",
        "",
    },
    "MQTT off",
    "Flow indispo",
    "Alarmes",
    "Mesure",
    "Ecran local pret",
    "Maintenir reset %lus pour reinit usine",
    "Continuer %lus pour reinit usine",
    "Reinit usine en cours...",
    "Reinit usine en echec...",
    "PIR inactif: retroeclairage off",
};

static const SupervisorHmiTextSet kTextEn{
    {
        "Low PSI",
        "High PSI",
        "pH empty",
        "Chlorine empty",
        "pH uptime",
        "ORP uptime",
        "Low water",
        "",
    },
    "MQTT off",
    "Flow unavailable",
    "Alarms",
    "Measure",
    "Local display ready",
    "Hold reset button %lus for factory reset",
    "Keep holding %lus for factory reset",
    "Factory reset starting...",
    "Factory reset failed...",
    "PIR idle: backlight off",
};

} // namespace

const SupervisorHmiTextSet* supervisorHmiTextSetDefault()
{
    return &kTextFr;
}

const SupervisorHmiTextSet* supervisorHmiTextSetForLanguage(const char* lang)
{
    if (isLangCode_(lang, 'e', 'n')) return &kTextEn;
    return &kTextFr;
}
