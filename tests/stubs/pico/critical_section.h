#ifndef XBOX_PICO_TEST_CRITICAL_SECTION_H
#define XBOX_PICO_TEST_CRITICAL_SECTION_H

typedef struct {
    int unused;
} critical_section_t;

static inline void critical_section_init(critical_section_t *section) {
    section->unused = 0;
}

static inline void critical_section_enter_blocking(critical_section_t *section) {
    (void)section;
}

static inline void critical_section_exit(critical_section_t *section) {
    (void)section;
}

#endif
