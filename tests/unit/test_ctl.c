#include "unicorn_test.h"
#include <time.h>
#include <string.h>

const uint64_t code_start = 0x1000;
const uint64_t code_len = 0x4000;

static void uc_common_setup(uc_engine **uc, uc_arch arch, uc_mode mode,
                            const char *code, uint64_t size)
{
    OK(uc_open(arch, mode, uc));
    OK(uc_mem_map(*uc, code_start, code_len, UC_PROT_ALL));
    OK(uc_mem_write(*uc, code_start, code, size));
}

#define GEN_SIMPLE_READ_TEST(field, ctl_type, arg_type, expected)              \
    static void test_uc_ctl_##field()                                          \
    {                                                                          \
        uc_engine *uc;                                                         \
        arg_type arg;                                                          \
        OK(uc_open(UC_ARCH_X86, UC_MODE_32, &uc));                             \
        OK(uc_ctl(uc, UC_CTL_READ(ctl_type, 1), &arg));                        \
        TEST_CHECK(arg == expected);                                           \
        OK(uc_close(uc));                                                      \
    }

GEN_SIMPLE_READ_TEST(mode, UC_CTL_UC_MODE, int, 4)
GEN_SIMPLE_READ_TEST(arch, UC_CTL_UC_ARCH, int, 4)
GEN_SIMPLE_READ_TEST(page_size, UC_CTL_UC_PAGE_SIZE, uint32_t, 4096)
GEN_SIMPLE_READ_TEST(time_out, UC_CTL_UC_TIMEOUT, uint64_t, 0)

static void test_uc_ctl_exits()
{
    uc_engine *uc;
    //   cmp eax, 0;
    //   jg lb;
    //   inc eax;
    //   nop;       <---- exit1
    // lb:
    //   inc ebx;
    //   nop;      <---- exit2
    char code[] = "\x83\xf8\x00\x7f\x02\x40\x90\x43\x90";
    int r_eax;
    int r_ebx;
    uint64_t exits[] = {code_start + 6, code_start + 8};

    uc_common_setup(&uc, UC_ARCH_X86, UC_MODE_32, code, sizeof(code) - 1);
    OK(uc_ctl_exits_enabled(uc, true));
    OK(uc_ctl_set_exists(uc, exits, 2));
    r_eax = 0;
    r_ebx = 0;
    OK(uc_reg_write(uc, UC_X86_REG_EAX, &r_eax));
    OK(uc_reg_write(uc, UC_X86_REG_EAX, &r_ebx));

    // Run two times.
    OK(uc_emu_start(uc, code_start, 0, 0, 0));
    OK(uc_emu_start(uc, code_start, 0, 0, 0));

    OK(uc_reg_read(uc, UC_X86_REG_EAX, &r_eax));
    OK(uc_reg_read(uc, UC_X86_REG_EAX, &r_ebx));

    TEST_CHECK(r_eax == 1);
    TEST_CHECK(r_ebx == 1);

    OK(uc_close(uc));
}

double time_emulation(uc_engine *uc, uint64_t start, uint64_t end)
{
    time_t t1, t2;

    t1 = clock();

    OK(uc_emu_start(uc, start, end, 0, 0));

    t2 = clock();

    return (t2 - t1) * 1000.0 / CLOCKS_PER_SEC;
}

#define TB_COUNT (8)
#define TCG_MAX_INSNS (512) // from tcg.h
#define CODE_LEN TB_COUNT *TCG_MAX_INSNS

static void test_uc_ctl_tb_cache()
{
    uc_engine *uc;
    char code[CODE_LEN];
    double standard, cached, evicted;

    memset(code, 0x90, CODE_LEN);

    uc_common_setup(&uc, UC_ARCH_X86, UC_MODE_32, code, sizeof(code) - 1);

    standard = time_emulation(uc, code_start, code_start + sizeof(code) - 1);

    for (int i = 0; i < TB_COUNT; i++) {
        OK(uc_ctl_request_cache(uc, code_start + i * TCG_MAX_INSNS));
    }

    cached = time_emulation(uc, code_start, code_start + sizeof(code) - 1);

    for (int i = 0; i < TB_COUNT; i++) {
        OK(uc_ctl_remove_cache(uc, code_start + i * TCG_MAX_INSNS));
    }
    evicted = time_emulation(uc, code_start, code_start + sizeof(code) - 1);

    // In fact, evicted is also slightly faster than standard but we don't do
    // this guarantee.
    TEST_CHECK(cached < standard);
    TEST_CHECK(evicted > cached);

    OK(uc_close(uc));
}

TEST_LIST = {{"test_uc_ctl_mode", test_uc_ctl_mode},
             {"test_uc_ctl_page_size", test_uc_ctl_page_size},
             {"test_uc_ctl_arch", test_uc_ctl_arch},
             {"test_uc_ctl_time_out", test_uc_ctl_time_out},
             {"test_uc_ctl_exits", test_uc_ctl_exits},
             {"test_uc_ctl_tb_cache", test_uc_ctl_tb_cache},
             {NULL, NULL}};