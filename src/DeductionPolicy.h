#ifndef DEDUCTION_POLICY_H
#define DEDUCTION_POLICY_H

// An unavailable Spoolman endpoint is terminal only when a presented tag has
// no writable weight target. Tag-absent MQTT applies must remain pending so a
// later physical scan can write the deduction.
constexpr bool deductionConsumesWithoutSpoolman(bool tagWasPresented) {
    return tagWasPresented;
}

#endif  // DEDUCTION_POLICY_H
