#include <stdio.h>

int foo(){
	return 2;
}

void f2(int i) {
  switch (i) {
  case 0: // something
    break;
  case 1: // something else
    break;
  }
  // All other numbers?
}

int main(){
	int i;
	for(i=0; i<10; i++){
		printf("%d\n", i);
	}

	if(i==5)
		printf("Hello\n");
	else
		printf("Bye\n");
	int x=0;

    switch (x = foo()){
        case 1: printf("Choice is 1\n");
                break;
        case 2: printf("Choice is 2\n");
        case 3: printf("Choice is 3\n");
        		break;
        default: printf("Choice other than 1, 2 and 3\n");
        		x=5;
        		break;
    }
	return 0;
}
