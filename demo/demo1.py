from pyclientlib import SkinnyClient


def main():
    c = SkinnyClient()
    while True:
        print("> ", end="")
        s = input()
        if not s:
            continue
        split = s.split()
        if split[0] == "open":
            print(c.Open(split[1]))
        elif split[0] == "write":
            print(c.SetContent(int(split[1]), split[2]))
        elif split[0] == "read":
            print(c.GetContent(int(split[1])))
        elif split[0] == "lock":
            print(c.Acquire(int(split[1]), True))
        elif split[0] == "unlock":
            print(c.Release(int(split[1])))
        elif split[0] == "trylock":
            print(c.TryAcquire(int(split[1]), True))


if __name__ == "__main__":
    main()
