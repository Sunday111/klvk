#include "fractal_app.hpp"
#include "klvk/error_handling.hpp"

void Main()
{
    FractalApp app;
    app.Run();
}

int main()
{
    klvk::ErrorHandling::InvokeAndCatchAll(Main);
    return 0;
}
