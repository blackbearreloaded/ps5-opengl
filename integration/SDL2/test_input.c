/* Real SDL + real PS5 joystick backend; only sce* calls below are doubles.
 * Copyright (C) 2026 BlackBearReloaded; SPDX-License-Identifier: GPL-3.0-or-later */
#include <SDL.h>
#include "SDL_ps5joystick.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static int users[4] = {-1, -1, -1, -1}, handles[4], opens, closes;
static PS5_PadData samples[4];
static const char *platform_failure;
extern void (*g42_frame)(void);
extern void g42_fail(const char *name);
extern int g42_input_main(int argc, char **argv);

static int fail(const char *name)
{
    if (!platform_failure || strcmp(platform_failure, name)) return 0;
    platform_failure = NULL; return -1;
}
int sceUserServiceInitialize(void *p) { assert(!p); return fail("user-init"); }
int scePadInit(void) { return fail("pad-init"); }
int sceUserServiceGetLoginUserIdList(int out[4])
{ if (fail("users")) return -1; memcpy(out, users, sizeof(users)); return 0; }
int sceUserServiceGetUserName(int id, char *out, size_t size)
{ assert(id >= 10 && id < 14); SDL_snprintf(out, size, "contract-%d", id); return 0; }
int scePadOpen(int id, int a, int b, void *p)
{
    assert(id >= 10 && id < 14 && !a && !b && !p && !handles[id - 10]);
    if (fail("open")) return -1;
    handles[id - 10] = 1; ++opens; return id + 100;
}
int scePadReadState(int handle, PS5_PadData *out)
{
    assert(handle >= 110 && handle < 114 && handles[handle - 110]);
    if (fail("read")) return -1;
    *out = samples[handle - 110]; return 0;
}
int scePadSetVibrationMode(int handle, int mode)
{ assert(handle >= 110 && handle < 114 && handles[handle - 110] && mode == 2); return fail("vibration"); }
int scePadSetVibration(int handle, const PS5_PadVibration *v)
{ assert(handle >= 110 && handle < 114 && handles[handle - 110] && v); return 0; }
int scePadSetLightBar(int handle, const PS5_PadColor *c)
{ assert(handle >= 110 && handle < 114 && handles[handle - 110] && c); return 0; }
int scePadClose(int handle)
{
    assert(handle >= 110 && handle < 114 && handles[handle - 110]);
    handles[handle - 110] = 0; ++closes; return fail("close");
}

static void reset(void)
{
    assert(SDL_WasInit(0) == 0 && opens == closes);
    for (int i = 0; i < 4; ++i) {
        assert(!handles[i]); users[i] = -1; SDL_zero(samples[i]);
        samples[i].leftStick.x = samples[i].leftStick.y = 128;
        samples[i].rightStick.x = samples[i].rightStick.y = 128;
    }
    platform_failure = NULL; g42_frame = NULL;
}
static int events(Uint32 type, SDL_JoystickID id)
{
    int count = 0;
    SDL_Event e;
    while (SDL_PollEvent(&e)) if (e.type == type && e.jdevice.which == id) ++count;
    return count;
}

static void driver_contract(void)
{
    SDL_Joystick *old, *fresh;
    SDL_JoystickID id;
    reset(); users[3] = 10; samples[0].connected = 1;
    assert(SDL_Init(SDL_INIT_JOYSTICK | SDL_INIT_TIMER) == 0);
    /* Sparse login slot must be exposed as SDL device zero. */
    assert(SDL_NumJoysticks() == 1);
    id = SDL_JoystickGetDeviceInstanceID(0); assert(id >= 0);
    old = SDL_JoystickOpen(0); assert(old && SDL_JoystickGetAttached(old));
    assert(SDL_JoystickOpen(0) == old); SDL_JoystickClose(old);
    assert(opens == closes + 1);
    events(SDL_JOYDEVICEADDED, 0);
    users[0] = 10; users[3] = -1; /* Login-list reorder is not a disconnect. */
    assert(events(SDL_JOYDEVICEREMOVED, id) == 0);
    assert(SDL_JoystickGetDeviceInstanceID(0) == id);
    samples[0].connected = 0;
    assert(events(SDL_JOYDEVICEREMOVED, id) == 1);
    assert(!SDL_JoystickGetAttached(old) && SDL_NumJoysticks() == 0);
    assert(events(SDL_JOYDEVICEREMOVED, id) == 0);
    samples[0].connected = 1;
    assert(events(SDL_JOYDEVICEADDED, 0) == 1);
    fresh = SDL_JoystickOpen(0); assert(fresh && fresh != old);
    assert(SDL_JoystickInstanceID(fresh) != id);
    SDL_JoystickClose(old); /* Must not close the reconnected device's handle. */
    assert(SDL_JoystickRumble(fresh, 1, 2, 0) == 0);
    SDL_JoystickClose(fresh);
    fresh = SDL_JoystickOpen(0); assert(fresh); /* Close/reopen owns one handle. */
    int index = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER, 2, 1, 0);
    assert(index >= 0 && SDL_JoystickGetDeviceInstanceID(index) != SDL_JoystickInstanceID(fresh));
    assert(SDL_JoystickDetachVirtual(index) == 0);
    id = SDL_JoystickInstanceID(fresh);
    platform_failure = "read"; SDL_ClearError(); SDL_JoystickUpdate();
    assert(strstr(SDL_GetError(), "scePadReadState"));
    assert(SDL_JoystickGetAttached(fresh)); /* Error is not disconnection evidence. */
    users[0] = 11; samples[1].connected = 1;
    assert(events(SDL_JOYDEVICEREMOVED, id) == 1);
    assert(SDL_NumJoysticks() == 1 && SDL_JoystickGetDeviceInstanceID(0) != id);
    SDL_JoystickClose(fresh); SDL_Quit(); reset();
    /* Enumerated-but-unopened handles are closed by driver Quit, too. */
    users[2] = 12; samples[2].connected = 0;
    assert(SDL_Init(SDL_INIT_JOYSTICK) == 0 && SDL_NumJoysticks() == 0);
    SDL_Quit(); reset();
    printf("G42 PS5 driver ownership/reconnect: PASS (platform doubles)\n");
}

static unsigned frame;
static const char *sequence;
static int virtual_index = -1;
static void tick(void)
{
    ++frame;
    if (!strcmp(sequence, "supported")) {
        switch (frame) {
        case 1: samples[0].buttons = PS5_PAD_BUTTON_CROSS; break;
        case 2: samples[0].buttons = 0; break;
        case 3: samples[0].leftStick.x = 255; break;
        case 4: samples[0].leftStick.x = 128; break;
        case 5: samples[0].connected = 0; break;
        case 7: samples[0].connected = 1; break;
        case 9: samples[0].buttons = PS5_PAD_BUTTON_CROSS; break;
        case 10: samples[0].buttons = 0; break;
        case 11: samples[0].leftStick.x = 0; break;
        case 12: samples[0].leftStick.x = 128; break;
        }
    } else if (!strcmp(sequence, "synthetic") && frame == 1) {
        SDL_Event e; SDL_zero(e);
        e.type = SDL_JOYBUTTONDOWN; e.jbutton.which = SDL_JoystickGetDeviceInstanceID(0);
        e.jbutton.button = 0; e.jbutton.state = SDL_PRESSED; assert(SDL_PushEvent(&e) == 1);
        e.type = SDL_JOYDEVICEREMOVED; assert(SDL_PushEvent(&e) == 1);
        e.type = SDL_JOYAXISMOTION; e.jaxis.value = 32000; assert(SDL_PushEvent(&e) == 1);
    } else if (!strcmp(sequence, "virtual") && frame == 1) {
        virtual_index = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER, 2, 1, 0);
        assert(virtual_index >= 0);
    } else if (!strcmp(sequence, "early-disconnect") && frame == 1) {
        samples[0].connected = 0;
    } else if (!strcmp(sequence, "read-error") && frame == 1) {
        platform_failure = "read";
    } else if ((!strcmp(sequence, "cleanup-error") || !strcmp(sequence, "detach-error")) && frame == 1) {
        if (!strcmp(sequence, "cleanup-error")) platform_failure = "close";
        else g42_fail("current");
        SDL_Event e; SDL_zero(e); e.type = SDL_QUIT; assert(SDL_PushEvent(&e) == 1);
    } else if (!strcmp(sequence, "quit") && frame == 1) {
        SDL_Event e; SDL_zero(e); e.type = SDL_QUIT; assert(SDL_PushEvent(&e) == 1);
    }
}
static void app_case(const char *name, int expected)
{
    reset(); sequence = name; frame = 0;
    if (strcmp(name, "virtual") && strcmp(name, "no-device")) {
        users[0] = 10; samples[0].connected = 1;
    }
    g42_frame = tick;
    assert(g42_input_main(0, NULL) == expected);
    reset();
}
void g42_contract(void)
{
    driver_contract();
    app_case("supported", 0);
    app_case("synthetic", 2);
    app_case("virtual", 2);
    app_case("no-device", 2);
    app_case("early-disconnect", 2);
    app_case("read-error", 1);
    app_case("quit", 2);
    app_case("cleanup-error", 1);
    app_case("detach-error", 1);
    for (unsigned i = 0; i < 5; ++i) {
        const char *names[] = {"user-init", "pad-init", "open", "vibration", "users"};
        reset(); users[0] = 10; samples[0].connected = 1;
        platform_failure = names[i];
        assert(g42_input_main(0, NULL) == 1); reset();
    }
    const char *failures[] = {"display", "initialize", "config", "surface", "width", "height",
                            "context", "current", "proc", "interval", "swap"};
    for (unsigned i = 0; i < SDL_arraysize(failures); ++i) {
        reset(); users[0] = 10; samples[0].connected = 1;
        g42_fail(failures[i]); assert(g42_input_main(0, NULL) == 1); reset();
    }
    app_case("supported", 0); /* Reinitialization after every failure. */
    printf("G42 input app contracts: PASS (no GPU or physical input acceptance)\n");
}
