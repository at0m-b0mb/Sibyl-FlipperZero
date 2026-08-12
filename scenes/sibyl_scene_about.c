#include "../sibyl_i.h"

void sibyl_scene_about_on_enter(void* context) {
    SibylApp* app = context;
    Widget* widget = app->widget;

    widget_reset(widget);

    widget_add_text_scroll_element(
        widget,
        0,
        0,
        128,
        64,
        "\e#Sibyl " SIBYL_VERSION "\e#\n"
        "Shazam for RF.\n"
        "\n"
        "Point it at a Sub-GHz signal and it\n"
        "tells you what kind of thing sent it,\n"
        "and how much that answer is worth.\n"
        "\n"
        "\e#Two kinds of answer\e#\n"
        "If the Flipper's own decoder stack\n"
        "recognises the packet, the protocol is\n"
        "a fact, not a guess. Some protocols\n"
        "belong to exactly one kind of product,\n"
        "and only those are ever CONFIRMED.\n"
        "\n"
        "If nothing decodes, Sibyl measures the\n"
        "signal instead: band, modulation, the\n"
        "symbol width, packet length, how many\n"
        "times it repeated. That narrows the\n"
        "field a great deal, but it is\n"
        "inference, so it is capped below\n"
        "CONFIRMED however neat it looks.\n"
        "\n"
        "\e#Naming a chip is not naming a\e#\n"
        "\e#device\e#\n"
        "Princeton, EV1527, Holtek and KeeLoq\n"
        "are encoder chips sold by the reel.\n"
        "They turn up in gate remotes,\n"
        "doorbells, mains sockets and PIR\n"
        "sensors alike. When one of those\n"
        "decodes, Sibyl says so and shows you\n"
        "the shortlist rather than picking\n"
        "whichever sounds best.\n"
        "\n"
        "\e#Reading the screen\e#\n"
        "Left and right walk the three pages:\n"
        "the answer, the shortlist, and the\n"
        "evidence with the captured packet\n"
        "drawn as a logic trace. If that trace\n"
        "does not look like a packet, do not\n"
        "believe the answer above it.\n"
        "\n"
        "OK opens the explainer for whatever\n"
        "you are pointing at.\n"
        "\n"
        "\e#While listening\e#\n"
        "Left and right change band. OK clears\n"
        "the capture. With Auto mod on, Sibyl\n"
        "walks the AM and FM presets while\n"
        "nothing is landing - an OOK preset is\n"
        "deaf to an FSK tyre sensor and the\n"
        "other way round.\n"
        "\n"
        "If you do not know the frequency, run\n"
        "Find Band and hold your remote down.\n"
        "\n"
        "\e#Listen only\e#\n"
        "Sibyl never transmits, replays or\n"
        "clones. It does not store codes and\n"
        "the session list lives in RAM only.\n"
        "\n"
        "Use it on your own equipment, or with\n"
        "permission.\n"
        "\n"
        "at0m-b0mb\n"
        "github.com/at0m-b0mb/Sibyl-FlipperZero\n");

    view_dispatcher_switch_to_view(app->view_dispatcher, SibylViewWidget);
}

bool sibyl_scene_about_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void sibyl_scene_about_on_exit(void* context) {
    SibylApp* app = context;
    widget_reset(app->widget);
}
