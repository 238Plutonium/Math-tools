class Solution {
public:
    int freq[10000005];
    bool isprime(int n){
        for(int i=2;i*i<=n;i++)
            if(n%i==0)
                return 0;
        return 1;
    }
    int mostFrequentPrime(vector<vector<int>>& mat) {
        for(int i=0;i<mat.size();i++)
            for(int j=0;j<mat[i].size();j++)
                for(int ii=-1;ii<=1;ii++)
                    for(int jj=-1;jj<=1;jj++)
                        if(ii||jj){
                            int curi=i,curj=j,val=0;
                            do{
                                val=val*10+mat[curi][curj];
                                if(val>10&&isprime(val))
                                    freq[val]++;
                                curi+=ii,curj+=jj;
                            }while(curi>=0&&curi<mat.size()&&curj>=0&&curj<mat[i].size());
                        }
        int ans=-1,maxfreq=0;
        for(int i=0;i<10000000;i++)
            if(freq[i]&&freq[i]>=maxfreq)
                ans=i,maxfreq=freq[i];
        return ans;
    }
};