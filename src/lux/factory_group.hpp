#pragma once


typedef struct FactoryGroup {
    int step;

    // ~~~ Methods:

    void finalize();

    void do_build();
    void do_water();
    void do_none();
} FactoryGroup;
