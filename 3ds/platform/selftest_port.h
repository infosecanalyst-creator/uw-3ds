// selftest_port.h - Underworld's additions to the shared selftest.c (3ds-test-suite hwtest/selftest).
// selftest.c includes this file twice: ST_PORT_CONFIG (settings) and ST_PORT_CODE (extra code).
#ifdef ST_PORT_CONFIG
// Underworld runs its game thread on core 2 at prio + 1, so the writer sits one step above it
// (prio): it sleeps between shots and only takes the core for the few ms each write needs (at
// prio + 2 it never ran once the game got busy: "4 files never written", Nov 18).
#define ST_WRITER_PRIO_OFFSET 0
#endif
#ifdef ST_PORT_CODE
// The system keyboard is its own applet: a script can't type into it, and the game draws no
// frames while it is open (the watchdog then sees a hang). In a scripted run it answers at once
// with a fixed test name (UW's character name, Nov 18). Needs -Wl,--wrap=swkbdInputText
// (tools/build3ds.py).
SwkbdButton __real_swkbdInputText(SwkbdState* swkbd, char* buf, size_t bufsize);
SwkbdButton __wrap_swkbdInputText(SwkbdState* swkbd, char* buf, size_t bufsize) {
    if (st_on == 1 && buf && bufsize > 0) {
        snprintf(buf, bufsize, "%s", "Tester");
        st_logf("swkbd: answered \"%s\"\n", buf);
        return SWKBD_BUTTON_CONFIRM;
    }
    return __real_swkbdInputText(swkbd, buf, bufsize);
}
#endif
