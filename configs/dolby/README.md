# Experimental Dolby tuning

This candidate is not referenced by the product makefiles and does not replace
sm8250-common's shipped Dolby configuration.

Based on the stock alioth dax-default.xml (SHA-256
5bce816dabc66b9ab3dbe3cee9454dee1aabeb49fefbd2aacc4e1b973a764042).
It sets speaker volmax-boost to zero in all profiles and disables
mi-dv-leveler-steering-enable in Dynamic and Mobility_default. Steering is
profile-wide, so that change also affects non-speaker endpoints. Other tuning
values and the leveler enable controls are preserved.

The candidate investigates reported loudness pumping with Volume Leveler enabled.
XML parsing succeeds; parameter readback does not establish audible improvement.
A temporary rooted-device bind mount was used for investigation. Production
integration requires comparison with stock at the same volume and passage,
including speaker and headphone routing. Do not interpret the numeric boost
values as decibels without establishing the vendor units.
