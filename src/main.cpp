#include <Arduino.h>

#include "App/Bootstrap.h"

void setup()
{
    Bootstrap::run();
}

void loop()
{
    Bootstrap::loop();
}
