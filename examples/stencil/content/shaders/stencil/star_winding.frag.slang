// The winding pass writes no color; a graphics pipeline still needs a fragment
// stage, so this one exists only to be discarded by the empty color write mask.

[shader("fragment")]
void main()
{
}
