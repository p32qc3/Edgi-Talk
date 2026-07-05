#ifndef EDGI_SPEC_FEATURES_H
#define EDGI_SPEC_FEATURES_H

#include <stdint.h>

/**
 * 40 维推理特征：谱图每行均值后做一阶差分（削弱「整体响度」、保留时序轮廓）。
 * 与 tools/generate_edgi_mlp_weights.py 中 spec_to_features 一致。
 */
void edgi_spec_row_features(const uint8_t spec[40][40], float out[40]);

#endif /* EDGI_SPEC_FEATURES_H */
