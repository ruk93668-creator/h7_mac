//UI1使用真实Device像素Ops，逐像素比较预期画面。
static bool Ui1_Pixel(const OledDevice_t *oled, uint32_t x, uint32_t y)
{
    return (oled->framebuffer[y / 8U * OLED_WIDTH + x] & (1U << (y % 8U))) != 0U;
}

static uint32_t Ui1_Case(uint32_t id)
{
    OledDevice_t *oled = &s_guard.oled;
    uint32_t x, y;
    static const uint8_t bitmap[] = {0x80, 0x80, 0x40, 0};
    static const char bounded[] = {' ', '~', 31, 127, (char)255};

    s_guard.before = 0x11223344U;
    s_guard.after = 0x55667788U;
    oled->base.state = DEVICE_STATE_READY;
    oled->ops = &s_test_ops;

    if (id == 61U)
    {
        CHECK(OledUi_DrawRect(oled, 0, 0, 128, 32, true) == SYS_OK);
        CHECK(OledUi_DrawLine(oled, 0, 15, 127, 15, true) == SYS_OK);
        CHECK(OledUi_DrawLine(oled, 64, 0, 64, 31, true) == SYS_OK);
        for (y = 0; y < 32; y++)
            for (x = 0; x < 128; x++)
                CHECK(Ui1_Pixel(oled, x, y) ==
                    (x == 0 || x == 127 || x == 64 || y == 0 || y == 31 || y == 15));
    }
    else if (id == 62U)
    {
        CHECK(OledUi_DrawLine(oled, -32768, -32768, 32767, 32767, true) == SYS_OK);
        CHECK(OledUi_DrawLine(oled, 31, 0, 0, 31, true) == SYS_OK);
        CHECK(OledUi_DrawLine(oled, 127, 31, 127, 31, true) == SYS_OK);
        CHECK(OledUi_DrawLine(oled, 4, -1, 0, 7, true) == SYS_OK);
        for (y = 0; y < 32; y++)
            for (x = 0; x < 128; x++)
                CHECK(Ui1_Pixel(oled, x, y) ==
                    (x == y || x + y == 31 || (x == 127 && y == 31) ||
                     (y <= 7 && x == (7U - y) / 2U)));
        CHECK(OledUi_DrawLine(oled, 32767, 32767, -32768, -32768, false) == SYS_OK);
        for (y = 0; y < 32; y++)
            CHECK(!Ui1_Pixel(oled, y, y));
    }
    else if (id == 63U)
    {
        CHECK(OledUi_FillRect(oled, -32768, -32768, 65535, 65535, true) == SYS_OK);
        CHECK(OledUi_FillRect(oled, -2, -3, 5, 6, false) == SYS_OK);
        CHECK(OledUi_DrawRect(oled, -2, 4, 7, 3, false) == SYS_OK);
        CHECK(OledUi_FillRect(oled, 0, 0, 0, 65535, false) == SYS_OK);
        CHECK(OledUi_DrawRect(oled, 0, 0, 65535, 0, false) == SYS_OK);
        CHECK(OledUi_FillRect(oled, 32767, 32767, 65535, 65535, false) == SYS_OK);
        for (y = 0; y < 32; y++)
            for (x = 0; x < 128; x++)
                CHECK(Ui1_Pixel(oled, x, y) ==
                    (!(x < 3 && y < 3) && !(y >= 4 && y <= 6 && x < 5 && (y != 5 || x == 4))));
    }
    else if (id == 64U)
    {
        memset(oled->framebuffer, 0xAA, OLED_BUFFER_SIZE);
        CHECK(OledUi_InvertArea(oled, -1, 7, 4, 11) == SYS_OK);
        for (y = 0; y < 32; y++)
            for (x = 0; x < 128; x++)
                CHECK(Ui1_Pixel(oled, x, y) == ((y % 2U != 0U) != (x < 3 && y >= 7 && y < 18)));
        CHECK(OledUi_InvertArea(oled, -1, 7, 4, 11) == SYS_OK);
        for (x = 0; x < OLED_BUFFER_SIZE; x++)
            CHECK(oled->framebuffer[x] == 0xAA);
    }
    else if (id == 65U)
    {
        static const uint8_t rows[] = {0x08, 0x36, 0x22, 0x41, 0x22, 0x36, 0x08};
        CHECK(OledUi_DrawRoundRect(oled, 5, 6, 7, 7, 65535, true) == SYS_OK);
        for (y = 0; y < 32; y++)
            for (x = 0; x < 128; x++)
                CHECK(Ui1_Pixel(oled, x, y) ==
                    (x >= 5 && x < 12 && y >= 6 && y < 13 && (rows[y - 6] & (1U << (x - 5))) != 0U));
    }
    else if (id == 66U)
    {
        CHECK(OledUi_DrawBitmap(oled, -1, 7, 9, 2, bitmap, 3) == SYS_ERR_INVALID_ARG);
        CHECK(OledUi_DrawBitmap(oled, 0, 0, 9, 2, NULL, 4) == SYS_ERR_INVALID_ARG);
        CHECK(OledUi_DrawBitmap(oled, -32768, -32768, 65535, 65535, bitmap, 4) == SYS_ERR_INVALID_ARG);
        CHECK(s_pixels == 0);
        CHECK(OledUi_DrawBitmap(oled, -1, 7, 9, 2, bitmap, sizeof(bitmap)) == SYS_OK);
        CHECK(s_pixels == 16);
        for (y = 0; y < 32; y++)
            for (x = 0; x < 128; x++)
                CHECK(Ui1_Pixel(oled, x, y) == ((x == 7 && y == 7) || (x == 0 && y == 8)));
    }
    else if (id == 67U)
    {
        static const uint8_t expected[] = {0,0,0,0,0,0, 8,4,8,16,8,0,
            2,1,0x51,9,6,0, 2,1,0x51,9,6,0, 2,1,0x51,9,6,0};
        CHECK(OledUi_DrawString(oled, 0, 0, NULL, 0) == SYS_ERR_INVALID_ARG);
        CHECK(OledUi_DrawString(oled, 0, 0, bounded, 0) == SYS_OK);
        CHECK(s_pixels == 0);
        CHECK(OledUi_DrawString(oled, 0, 0, bounded, sizeof(bounded)) == SYS_OK);
        CHECK(s_pixels == 240);
        CHECK(memcmp(oled->framebuffer, expected, sizeof(expected)) == 0);
        CHECK(OledUi_DrawChar(oled, -1, 30, 'A') == SYS_OK);
        CHECK(Ui1_Pixel(oled, 0, 30));
        CHECK(!Ui1_Pixel(oled, 0, 31));
        CHECK(OledUi_DrawString(oled, 125, -1, "AA", 2) == SYS_OK);
        CHECK(Ui1_Pixel(oled, 125, 0));
    }
    else if (id == 68U)
    {
        memset(oled->framebuffer, 0x37, OLED_BUFFER_SIZE);
        for (x = OLED_REFRESH_WAIT_CMD; x <= OLED_REFRESH_WAIT_DATA; x++)
        {
            oled->refresh_state = (OledRefreshState_t)x;
            CHECK(OledUi_DrawLine(oled, 0, 0, 1, 2, true) == SYS_ERR_BUSY);
            CHECK(OledUi_DrawRect(oled, 0, 0, 10, 10, true) == SYS_ERR_BUSY);
            CHECK(OledUi_FillRect(oled, 0, 0, 10, 10, true) == SYS_ERR_BUSY);
            CHECK(OledUi_DrawRoundRect(oled, 0, 0, 10, 10, 2, true) == SYS_ERR_BUSY);
            CHECK(OledUi_InvertArea(oled, 0, 0, 10, 10) == SYS_ERR_BUSY);
            CHECK(OledUi_DrawBitmap(oled, 0, 0, 9, 2, bitmap, sizeof(bitmap)) == SYS_ERR_BUSY);
            CHECK(OledUi_DrawChar(oled, 0, 0, 'A') == SYS_ERR_BUSY);
            CHECK(OledUi_DrawString(oled, 0, 0, bounded, sizeof(bounded)) == SYS_ERR_BUSY);
        }
        oled->refresh_state = OLED_REFRESH_IDLE;
        CHECK(OledUi_DrawLine(NULL, 0, 0, 1, 2, true) == SYS_ERR_INVALID_ARG);
        oled->ops = NULL;
        CHECK(OledUi_FillRect(oled, 0, 0, 1, 1, true) == SYS_ERR_NOT_SUPPORTED);
        oled->ops = &s_test_ops;
        oled->base.state = DEVICE_STATE_UNINIT;
        CHECK(OledUi_DrawChar(oled, 0, 0, 'A') == SYS_ERR_INVALID_STATE);
        CHECK(s_pixels == 0);
        for (x = 0; x < OLED_BUFFER_SIZE; x++)
            CHECK(oled->framebuffer[x] == 0x37);
    }
    CHECK(s_guard.before == 0x11223344U && s_guard.after == 0x55667788U);
    CHECK(s_tx_count == 0);
    return 0;
}
