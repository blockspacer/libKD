/*
 * ATX_keyboard.h
 *
 * This is really a preliminary header file for a preliminary keyboard extension by ATX!
 *
 * Based on version 3 from 2009-10-12
 *
 */

#ifndef __KD_ATX_KEYBOARD_H
#define __KD_ATX_KEYBOARD_H

#include <KD/kd.h>

#ifdef __cplusplus
extern "C" {
#endif
    #define KD_EVENT_INPUT_KEY_ATX 176


    #define KD_KEY_PRESS_ATX 1
    #define KD_KEY_LOCATION_LEFT_ATX 2
    #define KD_KEY_LOCATION_RIGHT_ATX 4
    #define KD_KEY_LOCATION_NUMPAD_ATX 8
    #define KD_KEY_MODIFIER_SHIFT_ATX 0x10
    #define KD_KEY_MODIFIER_CTRL_ATX 0x20
    #define KD_KEY_MODIFIER_ALT_ATX 0x40
    #define KD_KEY_MODIFIER_META_ATX 0x80

    #define KD_KEY_ACCEPT_ATX 0x200000
    #define KD_KEY_AGAIN_ATX 0x200001
    #define KD_KEY_ALLCANDIDATES_ATX 0x200002
    #define KD_KEY_ALPHANUMERIC_ATX 0x200003
    #define KD_KEY_ALT_ATX 0x200004
    #define KD_KEY_ALTGRAPH_ATX 0x200005
    #define KD_KEY_APPS_ATX 0x200006
    #define KD_KEY_ATTN_ATX 0x200007
    #define KD_KEY_BROWSERBACK_ATX 0x200008
    #define KD_KEY_BROWSERFAVORITES_ATX 0x200009
    #define KD_KEY_BROWSERFORWARD_ATX 0x20000a
    #define KD_KEY_BROWSERHOME_ATX 0x20000b
    #define KD_KEY_BROWSERREFRESH_ATX 0x20000c
    #define KD_KEY_BROWSERSEARCH_ATX 0x20000d
    #define KD_KEY_BROWSERSTOP_ATX 0x20000e
    #define KD_KEY_CAPSLOCK_ATX 0x20000f
    #define KD_KEY_CLEAR_ATX 0x200010
    #define KD_KEY_CODEINPUT_ATX 0x200011
    #define KD_KEY_COMPOSE_ATX 0x200012
    #define KD_KEY_CONTROL_ATX 0x200013
    #define KD_KEY_CRSEL_ATX 0x200014
    #define KD_KEY_CONVERT_ATX 0x200015
    #define KD_KEY_COPY_ATX 0x200016
    #define KD_KEY_CUT_ATX 0x200017
    #define KD_KEY_DOWN_ATX 0x200018
    #define KD_KEY_END_ATX 0x200019
    #define KD_KEY_ENTER_ATX 0x20001a
    #define KD_KEY_ERASEEOF_ATX 0x20001b
    #define KD_KEY_EXECUTE_ATX 0x20001c
    #define KD_KEY_EXSEL_ATX 0x20001d
    #define KD_KEY_F1_ATX 0x20001e
    #define KD_KEY_F2_ATX 0x20001f
    #define KD_KEY_F3_ATX 0x200020
    #define KD_KEY_F4_ATX 0x200021
    #define KD_KEY_F5_ATX 0x200022
    #define KD_KEY_F6_ATX 0x200023
    #define KD_KEY_F7_ATX 0x200024
    #define KD_KEY_F8_ATX 0x200025
    #define KD_KEY_F9_ATX 0x200026
    #define KD_KEY_F10_ATX 0x200027
    #define KD_KEY_F11_ATX 0x200028
    #define KD_KEY_F12_ATX 0x200029
    #define KD_KEY_F13_ATX 0x20002a
    #define KD_KEY_F14_ATX 0x20002b
    #define KD_KEY_F15_ATX 0x20002c
    #define KD_KEY_F16_ATX 0x20002d
    #define KD_KEY_F17_ATX 0x20002e
    #define KD_KEY_F18_ATX 0x20002f
    #define KD_KEY_F19_ATX 0x200030
    #define KD_KEY_F20_ATX 0x200031
    #define KD_KEY_F21_ATX 0x200032
    #define KD_KEY_F22_ATX 0x200033
    #define KD_KEY_F23_ATX 0x200034
    #define KD_KEY_F24_ATX 0x200035
    #define KD_KEY_FINALMODE_ATX 0x200036
    #define KD_KEY_FIND_ATX 0x200037
    #define KD_KEY_FULLWIDTH_ATX 0x200038
    #define KD_KEY_HALFWIDTH_ATX 0x200039
    #define KD_KEY_HANGULMODE_ATX 0x20003a
    #define KD_KEY_HANJAMODE_ATX 0x20003b
    #define KD_KEY_HELP_ATX 0x20003c
    #define KD_KEY_HIRAGANA_ATX 0x20003d
    #define KD_KEY_HOME_ATX 0x20003e
    #define KD_KEY_INSERT_ATX 0x20003f
    #define KD_KEY_JAPANESEHIRAGANA_ATX 0x200040
    #define KD_KEY_JAPANESEKATAKANA_ATX 0x200041
    #define KD_KEY_JAPANESEROMAJI_ATX 0x200042
    #define KD_KEY_JUNJAMODE_ATX 0x200043
    #define KD_KEY_KANAMODE_ATX 0x200044
    #define KD_KEY_KANJIMODE_ATX 0x200045
    #define KD_KEY_KATAKANA_ATX 0x200046
    #define KD_KEY_LAUNCHAPPLICATION1_ATX 0x200047
    #define KD_KEY_LAUNCHAPPLICATION2_ATX 0x200048
    #define KD_KEY_LAUNCHMAIL_ATX 0x200049
    #define KD_KEY_LEFT_ATX 0x20004a
    #define KD_KEY_META_ATX 0x20004b
    #define KD_KEY_MEDIANEXTTRACK_ATX 0x20004c
    #define KD_KEY_MEDIAPLAYPAUSE_ATX 0x20004d
    #define KD_KEY_MEDIAPREVIOUSTRACK_ATX 0x20004e
    #define KD_KEY_MEDIASTOP_ATX 0x20004f
    #define KD_KEY_MODECHANGE_ATX 0x200050
    #define KD_KEY_NONCONVERT_ATX 0x200051
    #define KD_KEY_NUMLOCK_ATX 0x200052
    #define KD_KEY_PAGEDOWN_ATX 0x200053
    #define KD_KEY_PAGEUP_ATX 0x200054
    #define KD_KEY_PASTE_ATX 0x200055
    #define KD_KEY_PAUSE_ATX 0x200056
    #define KD_KEY_PLAY_ATX 0x200057
    #define KD_KEY_PREVIOUSCANDIDATE_ATX 0x200058
    #define KD_KEY_PRINTSCREEN_ATX 0x200059
    #define KD_KEY_PROCESS_ATX 0x20005a
    #define KD_KEY_PROPS_ATX 0x20005b
    #define KD_KEY_RIGHT_ATX 0x20005c
    #define KD_KEY_ROMANCHARACTERS_ATX 0x20005d
    #define KD_KEY_SCROLL_ATX 0x20005e
    #define KD_KEY_SELECT_ATX 0x20005f
    #define KD_KEY_SELECTMEDIA_ATX 0x200060
    #define KD_KEY_SHIFT_ATX 0x200061
    #define KD_KEY_STOP_ATX 0x200062
    #define KD_KEY_UP_ATX 0x200063
    #define KD_KEY_UNDO_ATX 0x200064
    #define KD_KEY_VOLUMEDOWN_ATX 0x200065
    #define KD_KEY_VOLUMEMUTE_ATX 0x200066
    #define KD_KEY_VOLUMEUP_ATX 0x200067
    #define KD_KEY_WIN_ATX 0x200068
    #define KD_KEY_ZOOM_ATX 0x200069

    #define KD_EVENT_INPUT_KEYCHAR_ATX 464

    #define KD_KEY_AUTOREPEAT_ATX 0x200

    #define KD_IOGROUP_KEYBOARD_ATX 0x320
    #define KD_STATE_KEYBOARD_AVAILABILITY_ATX  (KD_IOGROUP_KEYBOARD_ATX + 0)
    #define KD_INPUT_KEYBOARD_FLAGS_ATX  (KD_IOGROUP_KEYBOARD_ATX + 1)
    #define KD_INPUT_KEYBOARD_CHAR_ATX  (KD_IOGROUP_KEYBOARD_ATX + 2)
    #define KD_INPUT_KEYBOARD_KEYCODE_ATX  (KD_IOGROUP_KEYBOARD_ATX + 3)
    #define KD_INPUT_KEYBOARD_CHARFLAGS_ATX  (KD_IOGROUP_KEYBOARD_ATX + 4)

// linux variant defined these in platform, I don't think they belong there.
typedef struct KDEventInputKeyATX {
    KDuint32 flags;
    KDint32 keycode;
} KDEventInputKeyATX;

typedef struct KDEventInputKeyCharATX {
    KDuint32 flags;
    KDint32 character;
} KDEventInputKeyCharATX;

#ifdef __cplusplus
}
#endif

#endif /* __KD_ATX_KEYBOARD_H */
