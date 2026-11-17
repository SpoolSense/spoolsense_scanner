#ifndef INPUT_MANAGER_H
#define INPUT_MANAGER_H

#if defined(BOARD_NO_KEYPAD)

// No-keypad boards: a compile-time stub keeps shared code (main.cpp) free of
// #ifdefs while the Keypad library and InputManager.cpp are left out.
class InputManager {
public:
    static InputManager& getInstance() { static InputManager instance; return instance; }
    void begin() {}
    void poll() {}
};

#else

#include <Keypad.h>

class InputManager {
public:
    static InputManager& getInstance();

    void begin();        // Configure keypad pins
    void poll();         // Check for key presses, enqueue messages (call from loop)

private:
    InputManager();
    InputManager(const InputManager&) = delete;
    InputManager& operator=(const InputManager&) = delete;

    static constexpr byte ROWS = 4;
    static constexpr byte COLS = 3;

    char keys_[ROWS][COLS];
    byte rowPins_[ROWS];
    byte colPins_[COLS];
    Keypad* keypad_ = nullptr;
    bool initialized_ = false;
};

#endif // BOARD_NO_KEYPAD

#endif // INPUT_MANAGER_H
