/* Run in the ARMv7 build container; virtual devices need no physical gamepad. */
#define main gof2hd_host_main
#include "../host/gof2hd.c"
#undef main
#include <assert.h>

static int attach_pad(const char* name, Uint16 product) {
    SDL_VirtualJoystickDesc desc = {0};
    desc.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
    desc.type = SDL_JOYSTICK_TYPE_UNKNOWN;
    desc.naxes = 4;
    desc.nbuttons = 17;
    desc.nhats = 1;
    desc.vendor_id = 0xface;
    desc.product_id = product;
    desc.name = name;
    int index = SDL_JoystickAttachVirtualEx(&desc);
    assert(index >= 0);
    return index;
}

int main(void) {
    assert(SDL_Init(SDL_INIT_GAMECONTROLLER) == 0);
    assert(SDL_NumJoysticks() == 0);
    assert(find_pad() == NULL);

    int unknown = attach_pad("ANBERNIC-keys", 1);
    int mapped = attach_pad("Mapped gamepad", 2);
    assert(!SDL_IsGameController(unknown));
    assert(!SDL_IsGameController(mapped));
    SDL_JoystickGUID guid = SDL_JoystickGetDeviceGUID(mapped);
    char guid_text[33], mapping[256];
    SDL_JoystickGetGUIDString(guid, guid_text, sizeof(guid_text));
    snprintf(mapping, sizeof(mapping), "%s,PortMaster test,a:b9,b:b8,platform:Linux,", guid_text);
    assert(SDL_GameControllerAddMapping(mapping) >= 0);
    SDL_GameController* pad = find_pad();
    assert(pad != NULL);
    assert(SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad)) ==
           SDL_JoystickGetDeviceInstanceID(mapped));
    SDL_GameControllerButtonBind bind = SDL_GameControllerGetBindForButton(pad, SDL_CONTROLLER_BUTTON_A);
    assert(bind.bindType == SDL_CONTROLLER_BINDTYPE_BUTTON && bind.value.button == 9);
    SDL_GameControllerClose(pad);

    assert(!SDL_IsGameController(unknown));

    for (int i = SDL_NumJoysticks() - 1; i >= 0; --i)
        assert(SDL_JoystickDetachVirtual(i) == 0);
    assert(find_pad() == NULL);
    SDL_Quit();
    puts("PASS: supplied SDL mapping, skip unmapped ANBERNIC, no controller");
    return 0;
}
