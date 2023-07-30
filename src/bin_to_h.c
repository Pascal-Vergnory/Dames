 #include <stdio.h>

int main(int argc, char* argv[])
{
    if (argc < 3) return 1;

    FILE* fi = fopen( argv[1], "rb");
    FILE* fo = fopen( argv[2], "w");

    int i = 0, l = 0;
    unsigned char c;

    fputs( " ", fo);
    while ((l = fread( &c, 1, 1, fi)) > 0) {
      if (i) {
        fputs( ",", fo);
        if ((i % 12) == 0) fputs( "\n ", fo);
      }  
      fprintf( fo, " 0x%02x", c );
      i++;
   }
   if ((i % 12) != 1) fputs( "\n", fo );
   fclose (fi);
   fclose (fo);

   return 0;
}
