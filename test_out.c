#include <stdio.h>

int foo(){
	return 2;
}

void f2(int i) {
   if(i ==  0){
 // something
    
  }else if(i ==  1){
 // something else
    
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

     if((x = foo()) ==  1){
 printf("Choice is 1\n");
                
        }else if((x = foo()) ==  2){
 printf("Choice is 2\n");
        printf("Choice is 3\n");
        		
        }else if((x = foo()) ==  3){
 printf("Choice is 3\n");
        		
        }else{
 printf("Choice other than 1, 2 and 3\n");
        		x=5;
        		
    }
	return 0;
}
