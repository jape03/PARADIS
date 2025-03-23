#include <iostream>
#include <fstream>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <openacc.h>

using namespace std;

int main() {
    const char* input_file_name  = "mars.bmp";
    const char* output_file_name = "grayscale_mars_parallel.bmp";

    ifstream input_stream(input_file_name, ios::binary);
    if (!input_stream) {
        cerr << "Error: Could not open " << input_file_name << endl;
        return 1;
    }

    char file_header[14];
    input_stream.read(file_header, 14);
    if (!input_stream) {
        cerr << "Error reading file header." << endl;
        return 1;
    }
    if (file_header[0] != 'B' || file_header[1] != 'M') {
        cerr << "Error: Not a valid BMP file." << endl;
        return 1;
    }

    uint32_t info_header_size;
    input_stream.read(reinterpret_cast<char*>(&info_header_size), sizeof(info_header_size));
    if (!input_stream) {
        cerr << "Error reading info header size." << endl;
        return 1;
    }

    size_t full_header_size = 14 + info_header_size;
    char* full_header = new char[full_header_size];

    memcpy(full_header, file_header, 14);
    memcpy(full_header + 14, &info_header_size, sizeof(info_header_size));

    input_stream.read(full_header + 14 + sizeof(info_header_size),
                      info_header_size - sizeof(info_header_size));
    if (!input_stream) {
        cerr << "Error reading full info header." << endl;
        delete[] full_header;
        return 1;
    }

    uint32_t data_offset     = *reinterpret_cast<uint32_t*>(full_header + 10);
    int32_t  bmp_width       = *reinterpret_cast<int32_t*>(full_header + 18);
    int32_t  bmp_height      = *reinterpret_cast<int32_t*>(full_header + 22);
    int16_t  bits_per_pixel  = *reinterpret_cast<int16_t*>(full_header + 28);

    if (bits_per_pixel != 24) {
        cerr << "Error: Not a valid 24-bit BMP file." << endl;
        delete[] full_header;
        return 1;
    }

    bool top_down = false;
    if (bmp_height < 0) {
        bmp_height = -bmp_height;
        top_down = true;
    }

    int32_t row_size   = ((bmp_width * 3) + 3) & ~3;
    int32_t image_size = row_size * bmp_height;

    char* pixel_data = new char[image_size];
    input_stream.seekg(data_offset, ios::beg);
    input_stream.read(pixel_data, image_size);
    if (!input_stream) {
        cerr << "Error reading pixel data." << endl;
        delete[] full_header;
        delete[] pixel_data;
        return 1;
    }
    input_stream.close();

    #pragma acc enter data copyin(pixel_data[0:image_size])

    auto start_time = chrono::high_resolution_clock::now();

    #pragma acc parallel loop collapse(2)
    for (int row_idx = 0; row_idx < bmp_height; row_idx++) {
        for (int col_idx = 0; col_idx < bmp_width; col_idx++) {
            
            int actual_row_idx = top_down ? row_idx : (bmp_height - 1 - row_idx);
            int row_start = actual_row_idx * row_size;

            int pixel_idx = row_start + col_idx * 3;
            unsigned char b = static_cast<unsigned char>(pixel_data[pixel_idx + 0]);
            unsigned char g = static_cast<unsigned char>(pixel_data[pixel_idx + 1]);
            unsigned char r = static_cast<unsigned char>(pixel_data[pixel_idx + 2]);

            unsigned char gray = static_cast<unsigned char>(
                0.299f * r + 0.587f * g + 0.114f * b
            );

            pixel_data[pixel_idx + 0] = gray;
            pixel_data[pixel_idx + 1] = gray;
            pixel_data[pixel_idx + 2] = gray;
        }
    }

    auto end_time = chrono::high_resolution_clock::now();
    #pragma acc update host(pixel_data[0:image_size])
    auto elapsed_time = chrono::duration_cast<chrono::microseconds>(end_time - start_time);

    cout << "Time: " << elapsed_time.count() << " microseconds - PARALLEL" << endl;

    uint32_t new_file_size = static_cast<uint32_t>(full_header_size + image_size);
    *reinterpret_cast<uint32_t*>(full_header + 2) = new_file_size;
    if (info_header_size >= 40) {
        *reinterpret_cast<uint32_t*>(full_header + 34) = image_size;
    }

    ofstream output_stream(output_file_name, ios::binary);
    if (!output_stream) {
        cerr << "Error: Could not open " << output_file_name << " for writing." << endl;
        delete[] full_header;
        delete[] pixel_data;
        return 1;
    }
    output_stream.write(full_header, full_header_size);
    output_stream.write(pixel_data, image_size);
    output_stream.close();

    delete[] full_header;
    delete[] pixel_data;
    return 0;
}

