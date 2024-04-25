#ifndef I2S_AUDIO_H
#define I2S_AUDIO_H

bool i2s_read_npm441(uint8_t *r_buf, size_t *r_bytes, int read_len);
bool i2s_write_max98357(uint16_t *w_buf, int w_lenth);

/**
 * 生成正弦波
 * @param w_buf 生成的正弦波数据
 * @param w_lenth 正弦波数据长度
 * @param s_rate 采样率
*/
void generate_sine_wave(uint16_t *w_buf, int w_lenth, int s_rate);

/**
 * 测试间隔1s播放440hz正弦波音频
*/
void test_i2s_audio(void);
void init_i2s_audio(void);
#endif