#include "fractal_app.hpp"
#include "klvk/error_handling.hpp"

void Main(int argc, char** argv)
{
    FractalApp app;
    app.RunWithArguments(argc, argv);
}

int main(int argc, char** argv)
{
    return klvk::ErrorHandling::InvokeAndCatchAll(Main, argc, argv);
}
