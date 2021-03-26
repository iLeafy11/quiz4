double compute_pi_leibniz(size_t N)
{
    double pi = 0.0;
    for (size_t i = 0; i < N; i++) {
        double tmp = (i & 1) ? (-1) : 1;
        pi += tmp / (2 * i + 1);
    }
    return pi * 4.0;
}
