// SPDX-License-Identifier: GPL-2.0-or-later
#include "shader.h"
#include <fstream>
#include <iostream>
#include <stdexcept>

int main(int argc, char** argv) {
    if (argc != 4) {
        std::cerr << "Usage: bb-metal-shader vertex|fragment|compute input.spv output.metal\n";
        return 2;
    }
    try {
        const std::string name = argv[1];
        const auto stage = name == "vertex" ? Bb::Metal::Stage::Vertex :
            name == "fragment" ? Bb::Metal::Stage::Fragment :
            name == "compute" ? Bb::Metal::Stage::Compute :
            throw std::runtime_error("Unknown shader stage");
        const auto shader = Bb::Metal::TranslateSpirv(Bb::Metal::ReadSpirv(argv[2]), stage);
        std::ofstream output(argv[3]);
        if (!output || !(output << shader.source)) throw std::runtime_error("Cannot write Metal source");
        std::cout << "Entry: " << shader.entry_point << "; push constants: " << shader.push_constant_size << " bytes\n";
        for (const auto& binding : shader.bindings) {
            std::cout << "set=" << binding.set << " binding=" << binding.binding << " count=" << binding.count;
            if (binding.buffer != Bb::Metal::Unbound) std::cout << " buffer=" << binding.buffer;
            if (binding.texture != Bb::Metal::Unbound) std::cout << " texture=" << binding.texture;
            if (binding.sampler != Bb::Metal::Unbound) std::cout << " sampler=" << binding.sampler;
            std::cout << '\n';
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
