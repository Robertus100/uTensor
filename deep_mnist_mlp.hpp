#include "mbed.h"
#include "tensor.hpp"
#include "uTensor_util.hpp"
#include "test.hpp"

template<typename T>
void tensorChkAlloc(Tensor<T> &t, Shape dim) {
  if(t.getSize() == 0) {
    t = Tensor<T>(dim);
  } else if (t.getShape() != dim) {
    ERR_EXIT("Dim mismatched...\r\n");
  }
}

void tensorQuantize(Tensor<float> input, Tensor<unsigned char> &output,
  Tensor<float> &out_min, Tensor<float> &out_max) {

    Tensor<int> reshape_shape({2});
    *(reshape_shape.getPointer({0})) = 1;  ///NT: WTF am I doing?
    *(reshape_shape.getPointer({1})) = -1;
    Tensor<int> reduce_dim({1});
    *(reduce_dim.getPointer({0})) = 0;

    Tensor<float> reshape_out;
    reshape(input, reshape_shape, reshape_out);
    input.~Tensor();
    

    Tensor<float> min_out({1});
    Min(reshape_out, reduce_dim, min_out);

    Tensor<float> max_out({1});
    Max(reshape_out, reduce_dim, max_out);

    tensorChkAlloc(output, reshape_out.getShape());
    uint32_t reshape_out0 = (reshape_out.getShape())[0];
    uint32_t reshape_out1 = (reshape_out.getShape())[1];
    Shape shape_one;
    shape_one.push_back(1);
    tensorChkAlloc(out_min, shape_one);
    tensorChkAlloc(out_max, shape_one);

    QuantizeV2(reshape_out, min_out, max_out, output, out_min, out_max);
}

void ReluLayer(Tensor<unsigned char> x, Tensor<float> x_min, Tensor<float> x_max,
   Tensor<unsigned char> w, Tensor<float> w_min, Tensor<float> w_max, Tensor<float> b,
    Tensor<unsigned char> &output, Tensor<float> &output_min, Tensor<float> &output_max) {
  
    uint32_t out_col = (x.getShape())[0];
    uint32_t out_row = (w.getShape())[1];
    printf("x.shape: %d, %d  w.shape: %d, %d\r\n", (x.getShape())[0], (x.getShape())[1], (w.getShape())[0], (w.getShape())[1]);
    fflush(stdout);
    Tensor<int> out_c({out_col, out_row});

    Tensor<float> matmul_out_min({1});
    Tensor<float> matmul_out_max({1});

    QuantizedMatMul<uint8_t, uint8_t, int>(x, w, out_c, x_min, w_min, x_max,
      w_max, matmul_out_min, matmul_out_max);
    //clean up
    x.~Tensor();
    w.~Tensor();
    x_min.~Tensor();
    w_min.~Tensor();
    x_max.~Tensor();
    w_max.~Tensor();

    Tensor<float> req_out_min({1});
    Tensor<float> req_out_max({1});
    Requantization_Range<int, float>(out_c, matmul_out_min, matmul_out_max, req_out_min, req_out_max);


    Tensor<unsigned char> reqnt_out(out_c.getShape());
    Tensor<float> reqnt_out_min({1});
    Tensor<float> reqnt_out_max({1});
    Requantize<int, float, unsigned char>(out_c, matmul_out_min, matmul_out_max, req_out_min, req_out_max,
      reqnt_out, reqnt_out_min, reqnt_out_max);

    Shape out_shape = out_c.getShape();
    //clean up
    out_c.~Tensor();
    matmul_out_min.~Tensor();
    matmul_out_max.~Tensor();
    req_out_min.~Tensor();
    req_out_max.~Tensor();

    Tensor<float> deqnt_out;
    dequantize(reqnt_out, reqnt_out_min, reqnt_out_max, deqnt_out);
    reqnt_out.~Tensor();

    Tensor<float> z_output(deqnt_out.getShape()); 
    Add<float, float>(deqnt_out, b, z_output);
    deqnt_out.~Tensor();

    Tensor<unsigned char> z_qnt_output;
    Tensor<float> z_min({1});
    Tensor<float> z_max({1});
    tensorQuantize(z_output, z_qnt_output, z_min, z_max);
    z_output.~Tensor();

    tensorChkAlloc(output, z_qnt_output.getShape());
    Shape shape_one;
    shape_one.push_back(1);
    tensorChkAlloc(output_min, shape_one);
    tensorChkAlloc(output_max, shape_one);
    Relu<unsigned char, float, unsigned char>(z_qnt_output, z_min, z_max, output, output_min, output_max);
}


void runMLP(void) {

  TensorIdxImporter t_import;
  Tensor<float> x = t_import.float_import("/fs/testData/deep_mlp/import-Placeholder_0.idx");
  Tensor<unsigned char> x_quantized;
  Tensor<float> x_min;
  Tensor<float> x_max;

  tensorQuantize(x, x_quantized, x_min, x_max);

  Tensor<unsigned char> w = t_import.ubyte_import("/fs/testData/deep_mlp/import-Variable_quint8_const_0.idx");
  Tensor<float> w_min = t_import.float_import("/fs/testData/deep_mlp/import-Variable_min_0.idx");
  Tensor<float> w_max = t_import.float_import("/fs/testData/deep_mlp/import-Variable_max_0.idx");
  Tensor<float> b = t_import.float_import("/fs/testData/deep_mlp/import-Variable_1_0.idx");
  Tensor<unsigned char> relu_output;
  Tensor<float> relu_min;
  Tensor<float> relu_max;

  ReluLayer(x_quantized, x_min, x_max, w, w_min, w_max, b, relu_output, relu_min, relu_max);

  w = t_import.ubyte_import("/fs/testData/deep_mlp/import-Variable_2_quint8_const_0.idx");
  w_min = t_import.float_import("/fs/testData/deep_mlp/import-Variable_2_min_0.idx");
  w_max = t_import.float_import("/fs/testData/deep_mlp/import-Variable_2_max_0.idx");
  b = t_import.float_import("/fs/testData/deep_mlp/import-Variable_3_0.idx");
  Tensor<unsigned char> relu_output2;
  Tensor<float> relu_min2;
  Tensor<float> relu_max2;

  ReluLayer(relu_output, relu_min, relu_max, w, w_min, w_max, b, relu_output2, relu_min2, relu_max2);
  w.~Tensor();

  Tensor<unsigned char> ref_relu_output2 = t_import.ubyte_import("/fs/testData/deep_mlp/out/import-Relu_1_eightbit_quantized_0.idx");
  Tensor<float> ref_relu_min2 = t_import.float_import("/fs/testData/deep_mlp/out/import-Relu_1_eightbit_quantized_1.idx");
  Tensor<float> ref_relu_max2 = t_import.float_import("/fs/testData/deep_mlp/out/import-Relu_1_eightbit_quantized_2.idx");

  double result = Test::meanPercentErr(ref_relu_output2, relu_output2);
  result += Test::meanPercentErr(ref_relu_min2, relu_min2);
  result += Test::meanPercentErr(ref_relu_max2, relu_max2);

  if(result < 0.0001) {
    printf("\r\nPASSED %.8\r\n", result);
  } else {
    printf("\r\nFAILED %.8\r\n", result);
  }


  //output layer

}
